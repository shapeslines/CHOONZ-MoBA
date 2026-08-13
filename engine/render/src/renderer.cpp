#include "render/renderer.h"
#include "render/pipeline_cache_check.h"
#include "render_batch.h"
#include "render_debug_draw.h"
#include "render_handle_table.h"
#include "render_submission_guard.h"
#include "shader_path.h"
#include "vk/vk.h"
#include "platform/platform.h"          // platform_log / platform_fatal / file I/O / arena
#include "platform/platform_vulkan.h"   // platform_vk_get_loader / platform_vk_create_surface
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

// Phase 2 renderer: dynamic rendering + synchronization2, typed DEVICE_LOCAL resources,
// per-frame camera/instance/debug rings, depth-correct batched meshes, and debug overlay.
// Startup uploads use HOST_COHERENT staging + one-shot submits. Frame pacing remains two
// frames in flight with per-frame acquire/fence state and per-swapchain-image present state.
#define FRAMES_IN_FLIGHT 2
#define MAX_SC_IMAGES    8
#define MAX_MESHES       256
#define MAX_TEXTURES     256
#define MAX_MATERIALS    512
#define MAX_DRAW_ITEMS   4096
#define MAX_DEBUG_VERTS  16384

struct MeshResource {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    uint32_t index_count;
    VkIndexType index_type;
};

struct TextureResource {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
};

struct MaterialResource {
    VkDescriptorSet descriptor_set;
    TextureHandle texture;
};

// ---- Static pipeline registry (roadmap M2.1) ----------------------------------
// Pipelines the renderer owns, created at startup from offline-compiled SPIR-V
// (ADR-0008: glslc -> ${build}/shaders, located via MOBA_SHADER_DIR).
enum PipelineVertexLayout {
    VERTEX_LAYOUT_POS3_UV2,    // MeshVertex: vec3 pos + vec2 uv from a vertex buffer
    VERTEX_LAYOUT_DEBUG,
};
enum PipelineLayoutKind {
    LAYOUT_MATERIAL,           // set=0 frame data + set=1 texture/sampler material
    LAYOUT_DEBUG,
};
struct PipelineDesc {
    const char* name;
    const char* vert_spv;      // file name inside MOBA_SHADER_DIR
    const char* frag_spv;
    PipelineVertexLayout vertex_layout;
    PipelineLayoutKind   layout_kind;
    VkPrimitiveTopology  topology;
    bool                 depth_test;
    bool                 depth_write;
};
enum { PIPELINE_MESH = 0, PIPELINE_DEBUG_WORLD = 1, PIPELINE_DEBUG_OVERLAY = 2, PIPELINE_COUNT = 3 };
static const PipelineDesc k_pipeline_registry[PIPELINE_COUNT] = {
    { "mesh", "mesh.vert.spv", "mesh.frag.spv", VERTEX_LAYOUT_POS3_UV2, LAYOUT_MATERIAL, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true },
    { "debug_world", "debug_world.vert.spv", "debug.frag.spv", VERTEX_LAYOUT_DEBUG, LAYOUT_DEBUG, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, false },
    { "debug_overlay", "debug_overlay.vert.spv", "debug.frag.spv", VERTEX_LAYOUT_DEBUG, LAYOUT_DEBUG, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false },
};

// Public POS3_UV2 mesh ABI used to validate typed mesh descriptors.
struct MeshVertex { float x, y, z, u, v; };
// On-disk pipeline cache (DoD: appears after first run, primes the second). Lives in
// the working directory until the asset/user-dir story lands (Phase 4).
// The size bound keeps a corrupt/foreign file from ever reaching the renderer arena's
// hard-abort overrun (the file is untrusted input; the arena policy is for budgets).
// Comfortably under the 16 MiB arena; today's real blobs are KB-scale.
static const char*  k_pipeline_cache_path = "vk_pipeline_cache.bin";
static const size_t k_pipeline_cache_max  = 8u * 1024 * 1024;

struct Renderer {
    Vk                       vk;
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         phys;
    VkPhysicalDeviceProperties props;        // kept: cache-blob validation + logs
    uint32_t                 gfx_family, present_family;
    VkDevice                 device;
    VkQueue                  gfx_queue, present_queue;

    // swapchain (recreated on resize)
    VkSwapchainKHR swapchain;
    VkFormat       sc_format;
    VkExtent2D     sc_extent;
    uint32_t       sc_count;
    VkImage        sc_images[MAX_SC_IMAGES];
    VkImageView    sc_views[MAX_SC_IMAGES];          // color-attachment views (M2.1)
    VkImage        depth_images[MAX_SC_IMAGES];
    VkDeviceMemory depth_mem[MAX_SC_IMAGES];
    VkImageView    depth_views[MAX_SC_IMAGES];
    VkFormat       depth_format;
    VkSemaphore    render_finished[MAX_SC_IMAGES];   // per image
    VkFence        images_in_flight[MAX_SC_IMAGES];  // borrowed frame fence, or NULL
    bool           need_recreate;
    bool           sc_can_transfer_src;              // capture/readback possible

    // pipelines (M2.1)
    VkPipelineCache  pipeline_cache;
    VkPipeline       pipelines[PIPELINE_COUNT];
    VkFormat         pipelines_format;               // sc format they were built for

    // vk_alloc Phase 1 (M2.2): dedicated allocations, counted against the cap.
    uint32_t         alloc_count;

    // material descriptors and shared sampler
    VkSampler             sampler;
    VkDescriptorSetLayout set0_layout;               // set=0: per-frame view/proj UBO (M2.3)
    VkDescriptorSetLayout material_layout;           // set=1: combined image sampler
    VkDescriptorPool      desc_pool;
    VkPipelineLayout      material_pipeline_layout;  // [set0_layout, material_layout]
    VkPipelineLayout      debug_pipeline_layout;

    // camera / per-frame UBO ring (M2.3): one persistently-mapped HOST_COHERENT buffer
    // + descriptor per frame slot. Written at the top of draw_frame — slot fr is safe
    // to touch because in_flight[fr] was just waited on (the old frame is done).
    struct CameraUBO { mm::mat4 view; mm::mat4 proj; };   // 128 B, 16-aligned, std140-clean
    CameraUBO             camera_ubo;                 // latest app camera (identity until set)
    VkBuffer              ubo[FRAMES_IN_FLIGHT];
    VkDeviceMemory        ubo_mem[FRAMES_IN_FLIGHT];
    void*                 ubo_map[FRAMES_IN_FLIGHT];
    VkDescriptorSet       ubo_set[FRAMES_IN_FLIGHT];
    VkBuffer              instance_buf[FRAMES_IN_FLIGHT];
    VkDeviceMemory        instance_mem[FRAMES_IN_FLIGHT];
    void*                 instance_map[FRAMES_IN_FLIGHT];
    VkBuffer              debug_vertex_buf[FRAMES_IN_FLIGHT];
    VkDeviceMemory        debug_vertex_mem[FRAMES_IN_FLIGHT];
    void*                 debug_vertex_map[FRAMES_IN_FLIGHT];
    RenderInstanceData*   instance_cpu;
    uint32_t              instance_count;

    RenderHandleTable mesh_handles;
    RenderHandleTable texture_handles;
    RenderHandleTable material_handles;
    MeshResource meshes[MAX_MESHES];
    TextureResource textures[MAX_TEXTURES];
    MaterialResource materials[MAX_MATERIALS];

    Arena frame_arenas[FRAMES_IN_FLIGHT];
    DrawItem* draw_items;
    uint32_t draw_count;
    RenderBatchOutput batch_output;
    RenderDebugList debug_draw;
    bool debug_overlay_started;
    int pending_fb_width;
    int pending_fb_height;
    bool pending_minimized;
    bool frame_begun;
    RendererStats stats;

    // per-frame
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd[FRAMES_IN_FLIGHT];
    VkSemaphore     image_available[FRAMES_IN_FLIGHT];
    VkFence         in_flight[FRAMES_IN_FLIGHT];
    uint32_t        frame;
    uint64_t        frame_count;

    Arena           arena;    // transient: .spv blobs, cache load/save (temp_begin/end)
    PlatformWindow* window;
};

// Validation: log WARN/ERROR, never abort the call. Perf/best-practices noise is kept
// out by the messenger's severity mask.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_cb(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types; (void)user;
    const char* msg = (data && data->pMessage) ? data->pMessage : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)        platform_log("[vk ERROR] %s\n", msg);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) platform_log("[vk WARN]  %s\n", msg);
    return VK_FALSE;
}

static bool has_layer(Vk* vk, const char* want) {
    uint32_t n = 0; vk->EnumerateInstanceLayerProperties(&n, nullptr);
    VkLayerProperties* props = (VkLayerProperties*)calloc(n ? n : 1, sizeof(VkLayerProperties));
    if (!props) return false;
    vk->EnumerateInstanceLayerProperties(&n, props);
    bool found = false;
    for (uint32_t i = 0; i < n; ++i) if (strcmp(props[i].layerName, want) == 0) { found = true; break; }
    free(props);
    return found;
}

static int device_type_score(VkPhysicalDeviceType t) {
    if (t == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   return 1000;
    if (t == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 100;
    return 10;
}

// ADR-0012 minimum spec: Vulkan 1.3 with dynamicRendering + synchronization2. A
// device below this is skipped during selection (clean "no device" error, no render-
// pass fallback path).
static bool device_meets_min_spec(Vk* vk, VkPhysicalDevice pd, const VkPhysicalDeviceProperties* props) {
    if (props->apiVersion < VK_API_VERSION_1_3) return false;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f13;
    vk->GetPhysicalDeviceFeatures2(pd, &f2);
    return f13.dynamicRendering == VK_TRUE && f13.synchronization2 == VK_TRUE;
}

// Find graphics + present queue families for `pd`/`surface`. Prefers one family that
// does both. Returns false if either is missing.
static bool pick_families(Vk* vk, VkPhysicalDevice pd, VkSurfaceKHR surface, uint32_t* gfx, uint32_t* present) {
    uint32_t qn = 0; vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    VkQueueFamilyProperties* qf = (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(VkQueueFamilyProperties));
    if (!qf) return false;
    vk->GetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    const uint32_t NONE = 0xffffffffu;
    uint32_t g = NONE, p = NONE;
    for (uint32_t i = 0; i < qn; ++i) {
        bool can_gfx = (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 can_present = VK_FALSE;
        vk->GetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &can_present);
        if (can_gfx && can_present) { g = i; p = i; break; }   // both in one family — ideal
        if (can_gfx && g == NONE) g = i;
        if (can_present && p == NONE) p = i;
    }
    free(qf);
    if (g == NONE || p == NONE) return false;
    *gfx = g; *present = p;
    return true;
}

// ---- vk_alloc Phase 1 (roadmap M2.0/M2.2): one VkDeviceMemory per resource --------
// The naive allocator: every buffer/image gets a dedicated allocation. Because buffers
// and images never share a VkDeviceMemory, bufferImageGranularity can't bite; because
// staging memory is HOST_COHERENT, no flush/nonCoherentAtomSize math exists anywhere.
// The live count stays conservatively under the 4096 maxMemoryAllocationCount floor —
// headroom for swapchain/driver internals — and is a hard ENSURE: blowing it means the
// naive scheme's time is up (the block allocator is a later, deliberate phase).
#define VK_ALLOC_MAX 3500u

// G25: warn well before the hard cap so the Phase 8 block-allocator trigger fires as
// a plan, not a crash. First warning at 90% of the cap, then every 100 allocations.
static void vk_alloc_pressure_check(Renderer* r) {
    if (r->alloc_count == VK_ALLOC_MAX * 9u / 10u) {
        platform_log("renderer: WARNING - %u dedicated allocations (90%% of the %u cap). "
                     "The Phase 8 block allocator trigger is near; plan it before real "
                     "assets land (G25).\n", r->alloc_count, VK_ALLOC_MAX);
    } else if (r->alloc_count > VK_ALLOC_MAX * 9u / 10u && (r->alloc_count % 100u) == 0u) {
        platform_log("renderer: WARNING - %u dedicated allocations; hard cap %u.\n",
                     r->alloc_count, VK_ALLOC_MAX);
    }
}

static uint32_t find_memory_type(Renderer* r, uint32_t type_bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    r->vk.GetPhysicalDeviceMemoryProperties(r->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static bool alloc_buffer(Renderer* r, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags mem_props, VkBuffer* out_buf, VkDeviceMemory* out_mem) {
    *out_buf = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (r->vk.CreateBuffer(r->device, &ci, nullptr, out_buf) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    r->vk.GetBufferMemoryRequirements(r->device, *out_buf, &req);
    uint32_t type = find_memory_type(r, req.memoryTypeBits, mem_props);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = type;
    ENSURE_MSG(r->alloc_count < VK_ALLOC_MAX, "vk_alloc: dedicated-allocation cap hit — time for the block allocator");
    if (type == UINT32_MAX ||
        r->vk.AllocateMemory(r->device, &mai, nullptr, out_mem) != VK_SUCCESS ||
        r->vk.BindBufferMemory(r->device, *out_buf, *out_mem, 0) != VK_SUCCESS) {
        if (*out_mem) r->vk.FreeMemory(r->device, *out_mem, nullptr);
        r->vk.DestroyBuffer(r->device, *out_buf, nullptr);
        *out_buf = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE;
        return false;
    }
    ++r->alloc_count;
    vk_alloc_pressure_check(r);
    return true;
}

static void free_buffer(Renderer* r, VkBuffer* buf, VkDeviceMemory* mem) {
    if (*buf) { r->vk.DestroyBuffer(r->device, *buf, nullptr); *buf = VK_NULL_HANDLE; }
    if (*mem) { r->vk.FreeMemory(r->device, *mem, nullptr); *mem = VK_NULL_HANDLE; --r->alloc_count; }
}

static bool alloc_image_2d(Renderer* r, uint32_t w, uint32_t h, VkFormat format,
                           VkImageUsageFlags usage, VkImage* out_img, VkDeviceMemory* out_mem) {
    *out_img = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE;
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = format;
    ci.extent        = { w, h, 1 };
    ci.mipLevels     = 1;                       // mip generation is the cooker's job (M4.2)
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (r->vk.CreateImage(r->device, &ci, nullptr, out_img) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    r->vk.GetImageMemoryRequirements(r->device, *out_img, &req);
    uint32_t type = find_memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = type;
    ENSURE_MSG(r->alloc_count < VK_ALLOC_MAX, "vk_alloc: dedicated-allocation cap hit — time for the block allocator");
    if (type == UINT32_MAX ||
        r->vk.AllocateMemory(r->device, &mai, nullptr, out_mem) != VK_SUCCESS ||
        r->vk.BindImageMemory(r->device, *out_img, *out_mem, 0) != VK_SUCCESS) {
        if (*out_mem) r->vk.FreeMemory(r->device, *out_mem, nullptr);
        r->vk.DestroyImage(r->device, *out_img, nullptr);
        *out_img = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE;
        return false;
    }
    ++r->alloc_count;
    vk_alloc_pressure_check(r);
    return true;
}

static void free_image(Renderer* r, VkImage* img, VkDeviceMemory* mem) {
    if (*img) { r->vk.DestroyImage(r->device, *img, nullptr); *img = VK_NULL_HANDLE; }
    if (*mem) { r->vk.FreeMemory(r->device, *mem, nullptr); *mem = VK_NULL_HANDLE; --r->alloc_count; }
}

static VkFormat choose_depth_format(Renderer* r) {
    static const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        VkFormatProperties props{};
        r->vk.GetPhysicalDeviceFormatProperties(r->phys, candidates[i], &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            return candidates[i];
    }
    return VK_FORMAT_UNDEFINED;
}

// sync2 image barrier helper: one full-subresource color transition.
static void image_barrier2(Renderer* r, VkCommandBuffer cb, VkImage image,
                           VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                           VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = src_stage;
    b.srcAccessMask       = src_access;
    b.dstStageMask        = dst_stage;
    b.dstAccessMask       = dst_access;
    b.oldLayout           = old_layout;
    b.newLayout           = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    r->vk.CmdPipelineBarrier2(cb, &dep);
}

static void depth_barrier2(Renderer* r, VkCommandBuffer cb, VkImage image) {
    VkImageMemoryBarrier2 b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    b.srcAccessMask       = VK_ACCESS_2_NONE;
    b.dstStageMask        = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    b.dstAccessMask       = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    r->vk.CmdPipelineBarrier2(cb, &dep);
}

// ---- One-shot submits (startup uploads; never per-frame) -------------------------
// Record into a transient command buffer, submit on the gfx queue, fence-wait to
// completion, free. Device-side visibility for later frame submissions comes from the
// barriers recorded INSIDE the one-shot (the fence only synchronizes the host).
static VkCommandBuffer begin_one_shot(Renderer* r) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = r->cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (r->vk.AllocateCommandBuffers(r->device, &ai, &cb) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r->vk.BeginCommandBuffer(cb, &bi);
    return cb;
}

static bool end_one_shot(Renderer* r, VkCommandBuffer cb) {
    r->vk.EndCommandBuffer(cb);
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    bool ok = r->vk.CreateFence(r->device, &fci, nullptr, &fence) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferSubmitInfo cbi{};
        cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cbi.commandBuffer = cb;
        VkSubmitInfo2 si{};
        si.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos    = &cbi;
        ok = r->vk.QueueSubmit2(r->gfx_queue, 1, &si, fence) == VK_SUCCESS &&
             r->vk.WaitForFences(r->device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
        r->vk.DestroyFence(r->device, fence, nullptr);
    }
    r->vk.FreeCommandBuffers(r->device, r->cmd_pool, 1, &cb);
    return ok;
}

// ---- Pipelines (M2.1) ----------------------------------------------------------

// Load one offline-compiled .spv from MOBA_SHADER_DIR into a shader module. The blob
// lives only inside the caller's TempMemory scope.
static VkShaderModule load_shader_module(Renderer* r, const char* spv_name) {
    char path[512];
    if (!render_shader_path(path, sizeof(path), MOBA_SHADER_DIR, spv_name)) {
        platform_log("renderer: shader path too long\n");
        return VK_NULL_HANDLE;
    }

    PlatformFile f;
    if (!platform_file_read(path, arena_allocator(&r->arena), &f)) {
        platform_log("renderer: shader missing: %s (build the 'shaders' target?)\n", path);
        return VK_NULL_HANDLE;
    }
    if (f.size == 0 || (f.size % 4) != 0) {
        platform_log("renderer: shader %s is not valid SPIR-V (%zu bytes)\n", path, f.size);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = f.size;
    ci.pCode    = (const uint32_t*)f.data;   // 16-aligned by platform_file_read
    VkShaderModule mod = VK_NULL_HANDLE;
    if (r->vk.CreateShaderModule(r->device, &ci, nullptr, &mod) != VK_SUCCESS) {
        platform_log("renderer: vkCreateShaderModule failed for %s\n", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

static void destroy_pipelines(Renderer* r) {
    for (int i = 0; i < PIPELINE_COUNT; ++i)
        if (r->pipelines[i]) { r->vk.DestroyPipeline(r->device, r->pipelines[i], nullptr); r->pipelines[i] = VK_NULL_HANDLE; }
}

// Build every registry pipeline against the CURRENT swapchain format, through the
// pipeline cache. Viewport/scissor are dynamic state, so a resize never rebuilds —
// only a (rare) surface-format change does.
static bool build_pipelines(Renderer* r) {
    for (int i = 0; i < PIPELINE_COUNT; ++i) {
        const PipelineDesc* d = &k_pipeline_registry[i];
        TempMemory tm = temp_begin(&r->arena);
        VkShaderModule vert = load_shader_module(r, d->vert_spv);
        VkShaderModule frag = load_shader_module(r, d->frag_spv);
        temp_end(tm);   // modules own a copy of the code; the blobs can go
        if (!vert || !frag) {
            if (vert) r->vk.DestroyShaderModule(r->device, vert, nullptr);
            if (frag) r->vk.DestroyShaderModule(r->device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName  = "main";

        // Vertex input is selected by registry entry: textured mesh or packed debug line.
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = d->vertex_layout == VERTEX_LAYOUT_DEBUG ? sizeof(RenderDebugVertex) : sizeof(MeshVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
        attrs[1].location = 1; attrs[1].binding = 0;
        attrs[1].format = d->vertex_layout == VERTEX_LAYOUT_DEBUG ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = sizeof(float) * 3;
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (d->vertex_layout == VERTEX_LAYOUT_POS3_UV2 || d->vertex_layout == VERTEX_LAYOUT_DEBUG) {
            vin.vertexBindingDescriptionCount   = 1;
            vin.pVertexBindingDescriptions      = &bind;
            vin.vertexAttributeDescriptionCount = 2;
            vin.pVertexAttributeDescriptions    = attrs;
        }

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = d->topology;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;     // pointers null: dynamic state below
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable  = d->depth_test ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = d->depth_write ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend_att{};
        blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blend_att;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dyn_states;

        // Dynamic rendering: the pipeline declares attachment formats here instead of
        // referencing a VkRenderPass (ADR-0012; no render-pass objects in this engine).
        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount    = 1;
        rendering.pColorAttachmentFormats = &r->sc_format;
        rendering.depthAttachmentFormat   = r->depth_format;

        VkGraphicsPipelineCreateInfo ci{};
        ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.pNext               = &rendering;
        ci.stageCount          = 2;
        ci.pStages             = stages;
        ci.pVertexInputState   = &vin;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState      = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState   = &ms;
        ci.pDepthStencilState  = &depth;
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dyn;
        ci.layout              = d->layout_kind == LAYOUT_MATERIAL ? r->material_pipeline_layout : r->debug_pipeline_layout;
        ci.renderPass          = VK_NULL_HANDLE;

        VkResult res = r->vk.CreateGraphicsPipelines(r->device, r->pipeline_cache, 1, &ci, nullptr, &r->pipelines[i]);
        r->vk.DestroyShaderModule(r->device, vert, nullptr);
        r->vk.DestroyShaderModule(r->device, frag, nullptr);
        if (res != VK_SUCCESS) {
            platform_log("renderer: vkCreateGraphicsPipelines failed for '%s' (%d)\n", d->name, (int)res);
            return false;
        }
    }
    r->pipelines_format = r->sc_format;
    return true;
}

// Load the on-disk cache if it is OURS (vendor/device/UUID validated — a stale or
// foreign blob is dropped and rebuilt, never fed to the driver), then create the
// VkPipelineCache, possibly primed.
static void create_pipeline_cache(Renderer* r) {
    VkPipelineCacheCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    TempMemory tm = temp_begin(&r->arena);
    PlatformFile f;
    size_t disk_size = 0;
    if (!platform_file_size(k_pipeline_cache_path, &disk_size)) {
        platform_log("renderer: no pipeline cache on disk — starting empty\n");
    } else if (disk_size > k_pipeline_cache_max) {
        // Size-check BEFORE the read: an oversized file would hard-abort in the fixed
        // arena, which is the budget policy's job, never an on-disk file's.
        platform_log("renderer: pipeline cache %s is oversized (%zu bytes) — starting empty\n", k_pipeline_cache_path, disk_size);
    } else if (platform_file_read(k_pipeline_cache_path, arena_allocator(&r->arena), &f)) {
        if (pipeline_cache_blob_ok(f.data, f.size, r->props.vendorID, r->props.deviceID,
                                   r->props.pipelineCacheUUID)) {
            ci.initialDataSize = f.size;
            ci.pInitialData    = f.data;
            platform_log("renderer: pipeline cache primed from %s (%zu bytes)\n", k_pipeline_cache_path, f.size);
        } else {
            platform_log("renderer: pipeline cache %s is stale/foreign — starting empty\n", k_pipeline_cache_path);
        }
    } else {
        platform_log("renderer: pipeline cache %s unreadable — starting empty\n", k_pipeline_cache_path);
    }
    if (r->vk.CreatePipelineCache(r->device, &ci, nullptr, &r->pipeline_cache) != VK_SUCCESS) {
        // Non-fatal: pipelines build fine without a cache, just slower.
        platform_log("renderer: vkCreatePipelineCache failed — continuing without\n");
        r->pipeline_cache = VK_NULL_HANDLE;
    }
    temp_end(tm);
}

static void save_pipeline_cache(Renderer* r) {
    if (!r->pipeline_cache) return;
    size_t n = 0;
    if (r->vk.GetPipelineCacheData(r->device, r->pipeline_cache, &n, nullptr) != VK_SUCCESS || n == 0) return;
    if (n > k_pipeline_cache_max) {   // driver-reported size: same bound as the load
        platform_log("renderer: pipeline cache too large to save (%zu bytes) — skipped\n", n);
        return;
    }
    TempMemory tm = temp_begin(&r->arena);
    void* data = arena_push(&r->arena, n, 16);
    if (r->vk.GetPipelineCacheData(r->device, r->pipeline_cache, &n, data) == VK_SUCCESS &&
        platform_file_write(k_pipeline_cache_path, data, n)) {
        platform_log("renderer: pipeline cache saved to %s (%zu bytes)\n", k_pipeline_cache_path, n);
    } else {
        platform_log("renderer: pipeline cache save failed (non-fatal)\n");
    }
    temp_end(tm);
}

// ---- Swapchain -------------------------------------------------------------------

static void destroy_swapchain(Renderer* r) {
    for (uint32_t i = 0; i < r->sc_count; ++i) {
        if (r->depth_views[i]) { r->vk.DestroyImageView(r->device, r->depth_views[i], nullptr); r->depth_views[i] = VK_NULL_HANDLE; }
        free_image(r, &r->depth_images[i], &r->depth_mem[i]);
        if (r->sc_views[i])        { r->vk.DestroyImageView(r->device, r->sc_views[i], nullptr); r->sc_views[i] = VK_NULL_HANDLE; }
        if (r->render_finished[i]) { r->vk.DestroySemaphore(r->device, r->render_finished[i], nullptr); r->render_finished[i] = VK_NULL_HANDLE; }
    }
    if (r->swapchain) { r->vk.DestroySwapchainKHR(r->device, r->swapchain, nullptr); r->swapchain = VK_NULL_HANDLE; }
    r->sc_count = 0;
}

// (Re)create the swapchain for the given framebuffer size. Caller has made the device
// idle. Returns false on a transient 0-extent (minimized) — caller should skip drawing.
static bool create_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    VkSurfaceCapabilitiesKHR caps{};
    r->vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(r->phys, r->surface, &caps);

    VkExtent2D extent;
    if (caps.currentExtent.width != 0xffffffffu) {
        extent = caps.currentExtent;
    } else {
        extent.width  = fb_w < caps.minImageExtent.width  ? caps.minImageExtent.width  : (fb_w > caps.maxImageExtent.width  ? caps.maxImageExtent.width  : fb_w);
        extent.height = fb_h < caps.minImageExtent.height ? caps.minImageExtent.height : (fb_h > caps.maxImageExtent.height ? caps.maxImageExtent.height : fb_h);
    }
    if (extent.width == 0 || extent.height == 0) return false;   // minimized

    // Format: prefer B8G8R8A8_SRGB / SRGB_NONLINEAR, else the first reported.
    uint32_t fn = 0; r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, nullptr);
    VkSurfaceFormatKHR* fmts = (VkSurfaceFormatKHR*)calloc(fn ? fn : 1, sizeof(VkSurfaceFormatKHR));
    r->vk.GetPhysicalDeviceSurfaceFormatsKHR(r->phys, r->surface, &fn, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fn; ++i)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = fmts[i]; break; }
    free(fmts);

    // Present mode: prefer MAILBOX (not guaranteed), else FIFO (always available).
    uint32_t pn = 0; r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, nullptr);
    VkPresentModeKHR* pmodes = (VkPresentModeKHR*)calloc(pn ? pn : 1, sizeof(VkPresentModeKHR));
    r->vk.GetPhysicalDeviceSurfacePresentModesKHR(r->phys, r->surface, &pn, pmodes);
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pn; ++i) if (pmodes[i] == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode = VK_PRESENT_MODE_MAILBOX_KHR; break; }
    free(pmodes);

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && want > caps.maxImageCount) want = caps.maxImageCount;

    // M2.1 renders into the image (COLOR_ATTACHMENT, always supported for swapchains);
    // TRANSFER_SRC is added when the surface allows an end-frame capture to read
    // pixels back. The M2.0 TRANSFER_DST clear usage is gone — loadOp clears now.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    r->sc_can_transfer_src = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (r->sc_can_transfer_src) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = r->surface;
    ci.minImageCount    = want;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = usage;
    uint32_t fams[2] = { r->gfx_family, r->present_family };
    if (r->gfx_family != r->present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = present_mode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    if (r->vk.CreateSwapchainKHR(r->device, &ci, nullptr, &r->swapchain) != VK_SUCCESS) {
        platform_log("renderer: vkCreateSwapchainKHR failed\n"); return false;
    }
    r->sc_format = chosen.format;
    r->sc_extent = extent;
    if (r->depth_format == VK_FORMAT_UNDEFINED) {
        r->depth_format = choose_depth_format(r);
        if (r->depth_format == VK_FORMAT_UNDEFINED) {
            platform_log("renderer: no supported depth-attachment format\n");
            return false;
        }
    }

    uint32_t cnt = 0; r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, nullptr);
    if (cnt > MAX_SC_IMAGES) platform_fatal("renderer: swapchain image count %u > MAX_SC_IMAGES\n", cnt);
    r->vk.GetSwapchainImagesKHR(r->device, r->swapchain, &cnt, r->sc_images);
    r->sc_count = cnt;

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < cnt; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = r->sc_images[i];
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = r->sc_format;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.layerCount     = 1;
        if (r->vk.CreateImageView(r->device, &vci, nullptr, &r->sc_views[i]) != VK_SUCCESS)
            platform_fatal("renderer: vkCreateImageView failed for swapchain image %u\n", i);

        if (!alloc_image_2d(r, extent.width, extent.height, r->depth_format,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            &r->depth_images[i], &r->depth_mem[i]))
            platform_fatal("renderer: depth image allocation failed for swapchain image %u\n", i);
        VkImageViewCreateInfo dvi{};
        dvi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvi.image                           = r->depth_images[i];
        dvi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        dvi.format                          = r->depth_format;
        dvi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        dvi.subresourceRange.levelCount     = 1;
        dvi.subresourceRange.layerCount     = 1;
        if (r->vk.CreateImageView(r->device, &dvi, nullptr, &r->depth_views[i]) != VK_SUCCESS)
            platform_fatal("renderer: depth image view failed for swapchain image %u\n", i);
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->render_finished[i]);
        r->images_in_flight[i] = VK_NULL_HANDLE;
    }
    return true;
}

static void recreate_swapchain(Renderer* r, uint32_t fb_w, uint32_t fb_h) {
    r->vk.DeviceWaitIdle(r->device);   // Phase-1 brute force (ARCHITECTURE / roadmap M2.0)
    destroy_swapchain(r);
    create_swapchain(r, fb_w, fb_h);
    r->need_recreate = false;

    // Pipelines bake the color-attachment format. A recreate normally keeps the same
    // surface format, but if it ever changes (driver/monitor oddity), rebuild.
    if (r->swapchain && r->pipelines[0] && r->pipelines_format != r->sc_format) {
        platform_log("renderer: swapchain format changed (%d -> %d) — rebuilding pipelines\n",
                     (int)r->pipelines_format, (int)r->sc_format);
        destroy_pipelines(r);
        if (!build_pipelines(r))
            platform_fatal("renderer: pipeline rebuild after format change failed\n");
    }
}

// ---- Create / destroy --------------------------------------------------------------

Renderer* renderer_create(PlatformWindow* window) {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)platform_vk_get_loader();
    if (!gipa) { platform_log("renderer: no Vulkan loader (vulkan-1.dll missing?)\n"); return nullptr; }

    Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));
    if (!r) return nullptr;
    r->window = window;
    if (!vk_load_global(&r->vk, gipa)) { free(r); return nullptr; }

    if (!platform_arena_reserve(&r->arena, 16u * 1024 * 1024)) { free(r); return nullptr; }
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (!platform_arena_reserve(&r->frame_arenas[i], 2u * 1024 * 1024)) {
            renderer_destroy(r);
            return nullptr;
        }
    }
    if (!render_handle_table_init(&r->mesh_handles, &r->arena, MAX_MESHES) ||
        !render_handle_table_init(&r->texture_handles, &r->arena, MAX_TEXTURES) ||
        !render_handle_table_init(&r->material_handles, &r->arena, MAX_MATERIALS)) {
        renderer_destroy(r);
        return nullptr;
    }

    const bool validation = has_layer(&r->vk, "VK_LAYER_KHRONOS_validation");
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char* exts[]   = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VkApplicationInfo app{};
    app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "moba";
    app.apiVersion       = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    if (validation) { ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers; }

    const VkResult instance_result = r->vk.CreateInstance(&ci, nullptr, &r->instance);
    if (instance_result != VK_SUCCESS) {
        if (instance_result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            platform_log("renderer: no Vulkan physical device or compatible driver\n");
        } else {
            platform_log("renderer: vkCreateInstance failed (%d)\n", (int)instance_result);
        }
        platform_arena_release(&r->arena);
        free(r);
        return nullptr;
    }
    vk_load_instance(&r->vk, r->instance);

    if (validation && r->vk.CreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debug_cb;
        r->vk.CreateDebugUtilsMessengerEXT(r->instance, &dci, nullptr, &r->debug);
    }

    // Surface (HWND stays in platform, ADR-0005).
    unsigned long long surf = 0;
    if (!platform_vk_create_surface(window, r->instance, &surf)) { platform_log("renderer: surface creation failed\n"); renderer_destroy(r); return nullptr; }
    r->surface = (VkSurfaceKHR)(uintptr_t)surf;

    // Pick the best physical device that has graphics+present, the swapchain ext, and
    // meets the ADR-0012 minimum spec.
    uint32_t pdn = 0; r->vk.EnumeratePhysicalDevices(r->instance, &pdn, nullptr);
    if (pdn == 0) { platform_log("renderer: no Vulkan physical devices\n"); renderer_destroy(r); return nullptr; }
    VkPhysicalDevice* pds = (VkPhysicalDevice*)calloc(pdn, sizeof(VkPhysicalDevice));
    if (!pds) { renderer_destroy(r); return nullptr; }
    r->vk.EnumeratePhysicalDevices(r->instance, &pdn, pds);

    int best = -1;
    for (uint32_t i = 0; i < pdn; ++i) {
        uint32_t g, p;
        VkPhysicalDeviceProperties props{};
        r->vk.GetPhysicalDeviceProperties(pds[i], &props);
        if (!device_meets_min_spec(&r->vk, pds[i], &props)) {
            platform_log("renderer: skipping %s — below minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2)\n", props.deviceName);
            continue;
        }
        if (!pick_families(&r->vk, pds[i], r->surface, &g, &p)) continue;
        int s = device_type_score(props.deviceType);
        if (s > best) { best = s; r->phys = pds[i]; r->gfx_family = g; r->present_family = p; r->props = props; }
    }
    free(pds);
    if (best < 0) {
        platform_log("renderer: no device meets the minimum spec (Vulkan 1.3 + dynamicRendering + synchronization2 + graphics/present) — ADR-0012\n");
        renderer_destroy(r);
        return nullptr;
    }

    // Logical device + queues (VK_KHR_swapchain). Up to two distinct queue families.
    // The 1.3 features the engine is built on are enabled here (gated above).
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qcis[2]{}; uint32_t qc = 0;
    qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->gfx_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    if (r->present_family != r->gfx_family) {
        qcis[qc].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qcis[qc].queueFamilyIndex = r->present_family; qcis[qc].queueCount = 1; qcis[qc].pQueuePriorities = &prio; ++qc;
    }
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext                   = &f13;
    dci.queueCreateInfoCount    = qc;
    dci.pQueueCreateInfos       = qcis;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    if (r->vk.CreateDevice(r->phys, &dci, nullptr, &r->device) != VK_SUCCESS) {
        platform_log("renderer: vkCreateDevice failed\n"); renderer_destroy(r); return nullptr;
    }
    vk_load_device(&r->vk, r->device);
    r->vk.GetDeviceQueue(r->device, r->gfx_family, 0, &r->gfx_queue);
    r->vk.GetDeviceQueue(r->device, r->present_family, 0, &r->present_queue);

    // Per-frame command buffers + sync.
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = r->gfx_family;
    r->vk.CreateCommandPool(r->device, &pci, nullptr, &r->cmd_pool);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = r->cmd_pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = FRAMES_IN_FLIGHT;
    r->vk.AllocateCommandBuffers(r->device, &ai, r->cmd);

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        r->vk.CreateSemaphore(r->device, &sci, nullptr, &r->image_available[i]);
        r->vk.CreateFence(r->device, &fci, nullptr, &r->in_flight[i]);
    }

    {
        // set=0: per-frame view/proj UBO at binding 0 and instance storage at binding 1.
        VkDescriptorSetLayoutBinding frame_binds[2]{};
        frame_binds[0].binding         = 0;
        frame_binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        frame_binds[0].descriptorCount = 1;
        frame_binds[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        frame_binds[1].binding         = 1;
        frame_binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        frame_binds[1].descriptorCount = 1;
        frame_binds[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo set0_ci{};
        set0_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0_ci.bindingCount = 2;
        set0_ci.pBindings    = frame_binds;

        VkDescriptorSetLayoutBinding tex_bind{};
        tex_bind.binding         = 0;
        tex_bind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_bind.descriptorCount = 1;
        tex_bind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo mat_ci{};
        mat_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        mat_ci.bindingCount = 1;
        mat_ci.pBindings    = &tex_bind;
        if (r->vk.CreateDescriptorSetLayout(r->device, &set0_ci, nullptr, &r->set0_layout) != VK_SUCCESS ||
            r->vk.CreateDescriptorSetLayout(r->device, &mat_ci, nullptr, &r->material_layout) != VK_SUCCESS) {
            platform_log("renderer: vkCreateDescriptorSetLayout failed\n"); renderer_destroy(r); return nullptr;
        }
        VkPushConstantRange debug_range{};
        debug_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        debug_range.size = sizeof(float) * 2;
        VkPipelineLayoutCreateInfo debug_plci{};
        debug_plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        debug_plci.setLayoutCount = 1;
        debug_plci.pSetLayouts = &r->set0_layout;
        debug_plci.pushConstantRangeCount = 1;
        debug_plci.pPushConstantRanges = &debug_range;
        if (r->vk.CreatePipelineLayout(r->device, &debug_plci, nullptr, &r->debug_pipeline_layout) != VK_SUCCESS) {
            platform_log("renderer: debug pipeline layout failed\n"); renderer_destroy(r); return nullptr;
        }
        VkDescriptorSetLayout sets[2] = { r->set0_layout, r->material_layout };
        VkPipelineLayoutCreateInfo mat_plci{};
        mat_plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        mat_plci.setLayoutCount = 2;
        mat_plci.pSetLayouts    = sets;
        VkPushConstantRange instance_base_range{};
        instance_base_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        instance_base_range.offset     = 0;
        instance_base_range.size       = sizeof(uint32_t);
        mat_plci.pushConstantRangeCount = 1;
        mat_plci.pPushConstantRanges    = &instance_base_range;
        if (r->vk.CreatePipelineLayout(r->device, &mat_plci, nullptr, &r->material_pipeline_layout) != VK_SUCCESS) {
            platform_log("renderer: material pipeline layout failed\n"); renderer_destroy(r); return nullptr;
        }

        // One pool for the whole descriptor life: FRAMES_IN_FLIGHT UBO sets (set=0) +
        // one material set (set=1). Sized up front; no per-frame allocation.
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = FRAMES_IN_FLIGHT;
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = MAX_MATERIALS;
        pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[2].descriptorCount = FRAMES_IN_FLIGHT;
        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets       = FRAMES_IN_FLIGHT + MAX_MATERIALS;
        pool_ci.poolSizeCount = 3;
        pool_ci.pPoolSizes    = pool_sizes;
        if (r->vk.CreateDescriptorPool(r->device, &pool_ci, nullptr, &r->desc_pool) != VK_SUCCESS) {
            platform_log("renderer: vkCreateDescriptorPool failed\n"); renderer_destroy(r); return nullptr;
        }
        // One fixed linear-repeat sampler — the "tiny fixed sampler set", population 1.
        VkSamplerCreateInfo samp_ci{};
        samp_ci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp_ci.magFilter    = VK_FILTER_LINEAR;
        samp_ci.minFilter    = VK_FILTER_LINEAR;
        samp_ci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;   // single mip until M4.2
        samp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samp_ci.maxLod       = VK_LOD_CLAMP_NONE;
        if (r->vk.CreateSampler(r->device, &samp_ci, nullptr, &r->sampler) != VK_SUCCESS) {
            platform_log("renderer: vkCreateSampler failed\n"); renderer_destroy(r); return nullptr;
        }
    }

    // Per-frame view/proj UBO ring (M2.3): HOST_VISIBLE|HOST_COHERENT, PERSISTENTLY
    // mapped, written directly each frame — no staging, no flush (ARCHITECTURE §8).
    // One buffer + one set=0 descriptor per frame slot; the draw writes only the slot
    // whose fence was just waited on, so a submitted frame never sees a mid-write UBO.
    {
        const VkDeviceSize ubo_bytes = sizeof(Renderer::CameraUBO);
        const VkDeviceSize instance_bytes = sizeof(RenderInstanceData) * MAX_DRAW_ITEMS;
        const VkDeviceSize debug_vertex_bytes = sizeof(RenderDebugVertex) * MAX_DEBUG_VERTS;
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            if (!alloc_buffer(r, ubo_bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &r->ubo[i], &r->ubo_mem[i])) {
                platform_log("renderer: per-frame UBO alloc failed\n"); renderer_destroy(r); return nullptr;
            }
            if (r->vk.MapMemory(r->device, r->ubo_mem[i], 0, ubo_bytes, 0, &r->ubo_map[i]) != VK_SUCCESS) {
                platform_log("renderer: UBO map failed\n"); renderer_destroy(r); return nullptr;
            }
            if (!alloc_buffer(r, instance_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &r->instance_buf[i], &r->instance_mem[i]) ||
                r->vk.MapMemory(r->device, r->instance_mem[i], 0, instance_bytes, 0, &r->instance_map[i]) != VK_SUCCESS) {
                platform_log("renderer: per-frame instance buffer alloc/map failed\n"); renderer_destroy(r); return nullptr;
            }
            if (!alloc_buffer(r, debug_vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &r->debug_vertex_buf[i], &r->debug_vertex_mem[i]) ||
                r->vk.MapMemory(r->device, r->debug_vertex_mem[i], 0, debug_vertex_bytes, 0, &r->debug_vertex_map[i]) != VK_SUCCESS) {
                platform_log("renderer: per-frame debug vertex buffer alloc/map failed\n"); renderer_destroy(r); return nullptr;
            }
            VkDescriptorSetAllocateInfo set_ai{};
            set_ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            set_ai.descriptorPool     = r->desc_pool;
            set_ai.descriptorSetCount = 1;
            set_ai.pSetLayouts        = &r->set0_layout;
            if (r->vk.AllocateDescriptorSets(r->device, &set_ai, &r->ubo_set[i]) != VK_SUCCESS) {
                platform_log("renderer: UBO descriptor set alloc failed\n"); renderer_destroy(r); return nullptr;
            }
            VkDescriptorBufferInfo infos[2]{};
            infos[0].buffer = r->ubo[i];
            infos[0].range  = ubo_bytes;
            infos[1].buffer = r->instance_buf[i];
            infos[1].range  = instance_bytes;
            VkWriteDescriptorSet writes[2]{};
            for (uint32_t binding = 0; binding < 2; ++binding) {
                writes[binding].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet          = r->ubo_set[i];
                writes[binding].dstBinding      = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType  = binding == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo     = &infos[binding];
            }
            r->vk.UpdateDescriptorSets(r->device, 2, writes, 0, nullptr);
        }
        // Identity until the app begins its first frame with a camera.
        r->camera_ubo.view = mm::mat4{};
        r->camera_ubo.view.m[0][0] = 1; r->camera_ubo.view.m[1][1] = 1;
        r->camera_ubo.view.m[2][2] = 1; r->camera_ubo.view.m[3][3] = 1;
        r->camera_ubo.proj = r->camera_ubo.view;
    }

    create_pipeline_cache(r);

    int32_t w = 0, h = 0; platform_window_size(window, &w, &h);
    create_swapchain(r, (uint32_t)(w > 0 ? w : 1), (uint32_t)(h > 0 ? h : 1));

    // Pipelines need the swapchain format; if the window started minimized the build
    // is deferred to the first successful (re)create inside the frame path.
    if (r->swapchain && !build_pipelines(r)) {
        platform_log("renderer: pipeline build failed — no renderer\n");
        renderer_destroy(r);
        return nullptr;
    }

    const char* kind = r->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? "discrete"
                     : r->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated" : "other";
    platform_log("renderer: Vulkan 1.3 up | validation=%s | GPU: %s (%s) | swapchain %ux%u x%u | %d pipeline(s)\n",
                 validation ? "on" : "off", r->props.deviceName, kind,
                 r->sc_extent.width, r->sc_extent.height, r->sc_count, (int)PIPELINE_COUNT);
    return r;
}

static void release_mesh_slot(void* user, uint32_t index) {
    Renderer* r = (Renderer*)user;
    MeshResource* mesh = &r->meshes[index];
    free_buffer(r, &mesh->vertex_buffer, &mesh->vertex_memory);
    free_buffer(r, &mesh->index_buffer, &mesh->index_memory);
    mesh->index_count = 0;
}

static void release_texture_slot(void* user, uint32_t index) {
    Renderer* r = (Renderer*)user;
    TextureResource* texture = &r->textures[index];
    if (texture->view) r->vk.DestroyImageView(r->device, texture->view, nullptr);
    texture->view = VK_NULL_HANDLE;
    free_image(r, &texture->image, &texture->memory);
}

static void release_material_slot(void* user, uint32_t index) {
    Renderer* r = (Renderer*)user;
    r->materials[index].texture = TextureHandle{HANDLE_NULL};
}

MeshHandle renderer_create_mesh(Renderer* r, const MeshDesc* desc) {
    MeshHandle result{HANDLE_NULL};
    if (!r || !desc || !desc->vertices || !desc->indices || desc->vertex_count == 0 ||
        desc->index_count == 0 || desc->vertex_layout != RENDERER_VERTEX_POS3_UV2 ||
        desc->vertex_stride < sizeof(MeshVertex)) return result;

    const VkDeviceSize vertex_bytes = (VkDeviceSize)desc->vertex_count * desc->vertex_stride;
    const uint32_t index_stride = desc->index_type == RENDERER_INDEX_U16 ? 2u : 4u;
    const VkDeviceSize index_bytes = (VkDeviceSize)desc->index_count * index_stride;
    const Handle raw = render_handle_alloc(&r->mesh_handles);
    if (handle_is_null(raw)) return result;
    MeshResource* mesh = &r->meshes[handle_index(raw)];
    mesh->index_count = desc->index_count;
    mesh->index_type = desc->index_type == RENDERER_INDEX_U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    bool ok = alloc_buffer(r, vertex_bytes + index_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &staging, &staging_memory) &&
              alloc_buffer(r, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mesh->vertex_buffer, &mesh->vertex_memory) &&
              alloc_buffer(r, index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mesh->index_buffer, &mesh->index_memory);
    void* mapped = nullptr;
    if (ok) ok = r->vk.MapMemory(r->device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
    if (ok) {
        memcpy(mapped, desc->vertices, (size_t)vertex_bytes);
        memcpy((uint8_t*)mapped + vertex_bytes, desc->indices, (size_t)index_bytes);
        r->vk.UnmapMemory(r->device, staging_memory);
        VkCommandBuffer cb = begin_one_shot(r);
        ok = cb != VK_NULL_HANDLE;
        if (ok) {
            VkBufferCopy copies[2]{};
            copies[0].size = vertex_bytes;
            copies[1].srcOffset = vertex_bytes;
            copies[1].size = index_bytes;
            r->vk.CmdCopyBuffer(cb, staging, mesh->vertex_buffer, 1, &copies[0]);
            r->vk.CmdCopyBuffer(cb, staging, mesh->index_buffer, 1, &copies[1]);
            VkBufferMemoryBarrier2 barriers[2]{};
            for (uint32_t i = 0; i < 2; ++i) {
                barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                barriers[i].srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
                barriers[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[i].size = VK_WHOLE_SIZE;
            }
            barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            barriers[0].buffer = mesh->vertex_buffer;
            barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
            barriers[1].buffer = mesh->index_buffer;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = 2;
            dep.pBufferMemoryBarriers = barriers;
            r->vk.CmdPipelineBarrier2(cb, &dep);
            ok = end_one_shot(r, cb);
        }
    }
    free_buffer(r, &staging, &staging_memory);
    if (!ok) {
        release_mesh_slot(r, handle_index(raw));
        render_handle_retire(&r->mesh_handles, raw, 0);
        render_handle_collect(&r->mesh_handles, 0, nullptr, nullptr);
        return result;
    }
    result.h = raw;
    return result;
}

TextureHandle renderer_create_texture(Renderer* r, const TextureDesc* desc) {
    TextureHandle result{HANDLE_NULL};
    if (!r || !desc || !desc->pixels || desc->width == 0 || desc->height == 0 ||
        desc->format != RENDERER_TEXTURE_RGBA8_SRGB ||
        desc->width > r->props.limits.maxImageDimension2D || desc->height > r->props.limits.maxImageDimension2D)
        return result;
    const Handle raw = render_handle_alloc(&r->texture_handles);
    if (handle_is_null(raw)) return result;
    TextureResource* texture = &r->textures[handle_index(raw)];
    const VkDeviceSize bytes = (VkDeviceSize)desc->width * desc->height * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    bool ok = alloc_image_2d(r, desc->width, desc->height, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             &texture->image, &texture->memory) &&
              alloc_buffer(r, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &staging, &staging_memory);
    void* mapped = nullptr;
    if (ok) ok = r->vk.MapMemory(r->device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
    if (ok) {
        memcpy(mapped, desc->pixels, (size_t)bytes);
        r->vk.UnmapMemory(r->device, staging_memory);
        VkCommandBuffer cb = begin_one_shot(r);
        ok = cb != VK_NULL_HANDLE;
        if (ok) {
            image_barrier2(r, cb, texture->image, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {desc->width, desc->height, 1};
            r->vk.CmdCopyBufferToImage(cb, staging, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            image_barrier2(r, cb, texture->image, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            ok = end_one_shot(r, cb);
        }
    }
    free_buffer(r, &staging, &staging_memory);
    if (ok) {
        VkImageViewCreateInfo view_ci{};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = texture->image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R8G8B8A8_SRGB;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.layerCount = 1;
        ok = r->vk.CreateImageView(r->device, &view_ci, nullptr, &texture->view) == VK_SUCCESS;
    }
    if (!ok) {
        release_texture_slot(r, handle_index(raw));
        render_handle_retire(&r->texture_handles, raw, 0);
        render_handle_collect(&r->texture_handles, 0, nullptr, nullptr);
        return result;
    }
    result.h = raw;
    return result;
}

MaterialHandle renderer_create_material(Renderer* r, const MaterialDesc* desc) {
    MaterialHandle result{HANDLE_NULL};
    if (!r || !desc || desc->sampler != RENDERER_SAMPLER_LINEAR_REPEAT ||
        !render_handle_valid(&r->texture_handles, desc->albedo.h)) return result;
    const Handle raw = render_handle_alloc(&r->material_handles);
    if (handle_is_null(raw)) return result;
    MaterialResource* material = &r->materials[handle_index(raw)];
    if (!material->descriptor_set) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = r->desc_pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &r->material_layout;
        if (r->vk.AllocateDescriptorSets(r->device, &ai, &material->descriptor_set) != VK_SUCCESS) {
            render_handle_retire(&r->material_handles, raw, 0);
            render_handle_collect(&r->material_handles, 0, nullptr, nullptr);
            return result;
        }
    }
    TextureResource* texture = &r->textures[handle_index(desc->albedo.h)];
    VkDescriptorImageInfo image_info{};
    image_info.sampler = r->sampler;
    image_info.imageView = texture->view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = material->descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    r->vk.UpdateDescriptorSets(r->device, 1, &write, 0, nullptr);
    material->texture = desc->albedo;
    result.h = raw;
    return result;
}

static uint64_t retire_after(const Renderer* r, uint32_t frames_until_free) {
    const uint32_t delay = frames_until_free < FRAMES_IN_FLIGHT ? FRAMES_IN_FLIGHT : frames_until_free;
    return r->frame_count + delay;
}

void renderer_destroy_mesh(Renderer* r, MeshHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->mesh_handles, h.h, retire_after(r, delay));
}
void renderer_destroy_texture(Renderer* r, TextureHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->texture_handles, h.h, retire_after(r, delay));
}
void renderer_destroy_material(Renderer* r, MaterialHandle h, uint32_t delay) {
    if (r) render_handle_retire(&r->material_handles, h.h, retire_after(r, delay));
}

void renderer_destroy(Renderer* r) {
    if (!r) return;
    if (r->device) r->vk.DeviceWaitIdle(r->device);
    if (r->device) {
        render_handle_release_all(&r->material_handles, release_material_slot, r);
        render_handle_release_all(&r->texture_handles, release_texture_slot, r);
        render_handle_release_all(&r->mesh_handles, release_mesh_slot, r);
    }
    if (r->device) save_pipeline_cache(r);   // before anything GPU-side is torn down
    destroy_pipelines(r);
    if (r->pipeline_cache)  r->vk.DestroyPipelineCache(r->device, r->pipeline_cache, nullptr);
    // M2.3 per-frame UBO ring (descriptor sets die with the pool).
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->ubo_map[i]) r->vk.UnmapMemory(r->device, r->ubo_mem[i]);
        free_buffer(r, &r->ubo[i], &r->ubo_mem[i]);
        if (r->instance_map[i]) r->vk.UnmapMemory(r->device, r->instance_mem[i]);
        free_buffer(r, &r->instance_buf[i], &r->instance_mem[i]);
        if (r->debug_vertex_map[i]) r->vk.UnmapMemory(r->device, r->debug_vertex_mem[i]);
        free_buffer(r, &r->debug_vertex_buf[i], &r->debug_vertex_mem[i]);
    }
    if (r->sampler)                  r->vk.DestroySampler(r->device, r->sampler, nullptr);
    if (r->desc_pool)                r->vk.DestroyDescriptorPool(r->device, r->desc_pool, nullptr);
    if (r->material_pipeline_layout) r->vk.DestroyPipelineLayout(r->device, r->material_pipeline_layout, nullptr);
    if (r->debug_pipeline_layout)    r->vk.DestroyPipelineLayout(r->device, r->debug_pipeline_layout, nullptr);
    if (r->set0_layout)              r->vk.DestroyDescriptorSetLayout(r->device, r->set0_layout, nullptr);
    if (r->material_layout)          r->vk.DestroyDescriptorSetLayout(r->device, r->material_layout, nullptr);

    destroy_swapchain(r);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->image_available[i]) r->vk.DestroySemaphore(r->device, r->image_available[i], nullptr);
        if (r->in_flight[i])       r->vk.DestroyFence(r->device, r->in_flight[i], nullptr);
    }
    if (r->cmd_pool) r->vk.DestroyCommandPool(r->device, r->cmd_pool, nullptr);
    if (r->device)   r->vk.DestroyDevice(r->device, nullptr);
    if (r->surface)  r->vk.DestroySurfaceKHR(r->instance, r->surface, nullptr);
    if (r->debug && r->vk.DestroyDebugUtilsMessengerEXT) r->vk.DestroyDebugUtilsMessengerEXT(r->instance, r->debug, nullptr);
    if (r->instance) r->vk.DestroyInstance(r->instance, nullptr);
    platform_arena_release(&r->arena);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        platform_arena_release(&r->frame_arenas[i]);
    free(r);
}

// ---- Frame -------------------------------------------------------------------------

// Pending readback for the frame being recorded (end-frame capture slow path).
struct CaptureState {
    VkBuffer       buffer;
    VkDeviceMemory memory;
    uint32_t       width, height;
    int            waited_frame;   // frame slot whose fence guards the copy, or -1
};

// Render + present one frame. When `cap` is non-null and the swapchain supports
// TRANSFER_SRC, the frame is also copied into a fresh host-visible buffer recorded in
// `cap` (caller waits the frame fence, maps, converts, frees). Returns false if no
// frame was submitted (minimized, transient acquire failure, swapchain gone).
static bool draw_frame(Renderer* r, int fb_width, int fb_height, bool minimized, CaptureState* cap) {
    if (cap) { cap->buffer = VK_NULL_HANDLE; cap->memory = VK_NULL_HANDLE; cap->waited_frame = -1; }
    if (!r || minimized || fb_width <= 0 || fb_height <= 0) return false;
    uint32_t fw = (uint32_t)fb_width, fh = (uint32_t)fb_height;

    if (r->need_recreate || r->swapchain == VK_NULL_HANDLE || fw != r->sc_extent.width || fh != r->sc_extent.height) {
        recreate_swapchain(r, fw, fh);
        if (r->swapchain == VK_NULL_HANDLE) return false;   // still minimized
    }
    // Deferred first build (window opened minimized) — needs the swapchain format.
    if (!r->pipelines[0] && !build_pipelines(r))
        platform_fatal("renderer: deferred pipeline build failed\n");

    uint32_t fr = r->frame;
    r->vk.WaitForFences(r->device, 1, &r->in_flight[fr], VK_TRUE, UINT64_MAX);
    render_handle_collect(&r->material_handles, r->frame_count, release_material_slot, r);
    render_handle_collect(&r->texture_handles, r->frame_count, release_texture_slot, r);
    render_handle_collect(&r->mesh_handles, r->frame_count, release_mesh_slot, r);

    uint32_t img = 0;
    VkResult acq = r->vk.AcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->image_available[fr], VK_NULL_HANDLE, &img);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { r->need_recreate = true; return false; }   // no image -> semaphore NOT signaled, safe to bail
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) { platform_log("renderer: acquire failed (%d)\n", (int)acq); return false; }

    // Don't write an image a prior frame is still presenting.
    if (r->images_in_flight[img] != VK_NULL_HANDLE)
        r->vk.WaitForFences(r->device, 1, &r->images_in_flight[img], VK_TRUE, UINT64_MAX);
    r->images_in_flight[img] = r->in_flight[fr];
    r->vk.ResetFences(r->device, 1, &r->in_flight[fr]);

    // Optional readback buffer for this frame (created at the now-final extent).
    bool capture = false;
    if (cap && r->sc_can_transfer_src) {
        VkDeviceSize bytes = (VkDeviceSize)r->sc_extent.width * r->sc_extent.height * 4;
        capture = alloc_buffer(r, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &cap->buffer, &cap->memory);
        if (capture) {
            cap->width  = r->sc_extent.width;
            cap->height = r->sc_extent.height;
        } else {   // fall back to a plain frame; caller sees waited_frame == -1
            platform_log("renderer: capture buffer setup failed — drawing without readback\n");
        }
    }

    // Animated clear color (presentation only — float is fine here).
    double t = (double)r->frame_count * 0.02;
    VkClearValue clear{};
    clear.color.float32[0] = (float)(0.5 + 0.5 * sin(t));
    clear.color.float32[1] = (float)(0.5 + 0.5 * sin(t + 2.094));
    clear.color.float32[2] = (float)(0.5 + 0.5 * sin(t + 4.188));
    clear.color.float32[3] = 1.0f;

    // Per-frame UBO (M2.3): slot fr is free (fence just waited) — write the camera
    // in. HOST_COHERENT: visible to the GPU without a flush.
    memcpy(r->ubo_map[fr], &r->camera_ubo, sizeof(Renderer::CameraUBO));
    if (r->instance_count > 0)
        memcpy(r->instance_map[fr], r->instance_cpu, sizeof(RenderInstanceData) * r->instance_count);
    if (r->debug_draw.count > 0)
        memcpy(r->debug_vertex_map[fr], r->debug_draw.vertices, sizeof(RenderDebugVertex) * r->debug_draw.count);

    VkCommandBuffer cb = r->cmd[fr];
    r->vk.ResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r->vk.BeginCommandBuffer(cb, &bi);

    // UNDEFINED -> COLOR_ATTACHMENT. src stage = COLOR_ATTACHMENT_OUTPUT chains the
    // transition after the image_available semaphore wait (which waits at that stage).
    image_barrier2(r, cb, r->sc_images[img],
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth_barrier2(r, cb, r->depth_images[img]);

    VkRenderingAttachmentInfo color{};
    color.sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView        = r->sc_views[img];
    color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;     // the M2.0 clear lives on as loadOp
    color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue       = clear;

    VkRenderingAttachmentInfo depth{};
    depth.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView   = r->depth_views[img];
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent    = r->sc_extent;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color;
    ri.pDepthAttachment     = &depth;
    r->vk.CmdBeginRendering(cb, &ri);

    VkViewport vp{};
    vp.width    = (float)r->sc_extent.width;
    vp.height   = (float)r->sc_extent.height;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = r->sc_extent;
    r->vk.CmdSetViewport(cb, 0, 1, &vp);
    r->vk.CmdSetScissor(cb, 0, 1, &scissor);

    r->stats.scene_draw_calls = 0;

    for (uint32_t batch_index = 0; batch_index < r->batch_output.batch_count; ++batch_index) {
        const RenderBatch* batch = &r->batch_output.batches[batch_index];
        const MeshResource* mesh = &r->meshes[handle_index(batch->mesh.h)];
        const MaterialResource* material = &r->materials[handle_index(batch->material.h)];
        VkDeviceSize vb_offset = 0;
        r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_MESH]);
        r->vk.CmdBindVertexBuffers(cb, 0, 1, &mesh->vertex_buffer, &vb_offset);
        r->vk.CmdBindIndexBuffer(cb, mesh->index_buffer, 0, mesh->index_type);
        // set=0 per-frame view/proj UBO (M2.3) + set=1 material — both sets at once.
        VkDescriptorSet sets[2] = { r->ubo_set[fr], material->descriptor_set };
        r->vk.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->material_pipeline_layout,
                                    0, 2, sets, 0, nullptr);
        const uint32_t instance_base = batch->instance_base;
        r->vk.CmdPushConstants(cb, r->material_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(instance_base), &instance_base);
        r->vk.CmdDrawIndexed(cb, mesh->index_count, batch->instance_count, 0, 0, 0);
        ++r->stats.scene_draw_calls;
    }
    r->stats.total_draw_calls = r->stats.scene_draw_calls;

    if (r->debug_draw.world_count > 0) {
        const VkDeviceSize offset = 0;
        r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_DEBUG_WORLD]);
        r->vk.CmdBindVertexBuffers(cb, 0, 1, &r->debug_vertex_buf[fr], &offset);
        r->vk.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->debug_pipeline_layout,
                                    0, 1, &r->ubo_set[fr], 0, nullptr);
        r->vk.CmdDraw(cb, r->debug_draw.world_count, 1, 0, 0);
        ++r->stats.total_draw_calls;
    }
    const uint32_t overlay_count = r->debug_draw.count - r->debug_draw.world_count;
    if (overlay_count > 0) {
        const VkDeviceSize offset = 0;
        const float viewport_size[2] = {(float)r->sc_extent.width, (float)r->sc_extent.height};
        r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_DEBUG_OVERLAY]);
        r->vk.CmdBindVertexBuffers(cb, 0, 1, &r->debug_vertex_buf[fr], &offset);
        r->vk.CmdPushConstants(cb, r->debug_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(viewport_size), viewport_size);
        r->vk.CmdDraw(cb, overlay_count, 1, r->debug_draw.world_count, 0);
        ++r->stats.total_draw_calls;
    }

    r->vk.CmdEndRendering(cb);

    if (capture) {
        // COLOR_ATTACHMENT -> TRANSFER_SRC, copy to the host buffer, then -> PRESENT.
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width  = r->sc_extent.width;
        region.imageExtent.height = r->sc_extent.height;
        region.imageExtent.depth  = 1;
        r->vk.CmdCopyImageToBuffer(cb, r->sc_images[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cap->buffer, 1, &region);
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    } else {
        // COLOR_ATTACHMENT -> PRESENT. Visibility to the presentation engine comes
        // from the render_finished semaphore, hence dst NONE (sync2 allows it).
        image_barrier2(r, cb, r->sc_images[img],
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    r->vk.EndCommandBuffer(cb);

    VkSemaphoreSubmitInfo wait{};
    wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = r->image_available[fr];
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{};
    signal.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = r->render_finished[img];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{};
    cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = cb;
    VkSubmitInfo2 si{};
    si.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount   = 1;
    si.pWaitSemaphoreInfos      = &wait;
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos    = &signal;
    r->vk.QueueSubmit2(r->gfx_queue, 1, &si, r->in_flight[fr]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &r->render_finished[img];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &r->swapchain;
    pi.pImageIndices      = &img;
    VkResult pres = r->vk.QueuePresentKHR(r->present_queue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) r->need_recreate = true;
    else if (pres != VK_SUCCESS) platform_log("renderer: present failed (%d)\n", (int)pres);

    if (capture) cap->waited_frame = (int)fr;
    r->frame = (fr + 1) % FRAMES_IN_FLIGHT;
    ++r->frame_count;
    return true;
}

static bool capture_frame(Renderer* r, int fb_width, int fb_height, bool minimized,
                          Allocator alloc, RendererCapture* out) {
    if (!r || !out) return false;
    if (!r->sc_can_transfer_src) {
        platform_log("renderer: capture unsupported (swapchain has no TRANSFER_SRC)\n");
        return false;
    }
    // Only 32-bit BGRA/RGBA swapchain formats are handled (every format this engine
    // selects; see create_swapchain's preference order).
    const bool bgra = r->sc_format == VK_FORMAT_B8G8R8A8_SRGB || r->sc_format == VK_FORMAT_B8G8R8A8_UNORM;
    const bool rgba = r->sc_format == VK_FORMAT_R8G8B8A8_SRGB || r->sc_format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!bgra && !rgba) {
        platform_log("renderer: capture unsupported for swapchain format %d\n", (int)r->sc_format);
        return false;
    }

    CaptureState cap;
    bool drew = draw_frame(r, fb_width, fb_height, minimized, &cap);
    if (!drew || cap.waited_frame < 0) {
        free_buffer(r, &cap.buffer, &cap.memory);
        return false;   // transient (minimized/OUT_OF_DATE) — caller may retry
    }

    // The frame fence covers the copy; HOST_COHERENT makes it visible after the wait.
    r->vk.WaitForFences(r->device, 1, &r->in_flight[cap.waited_frame], VK_TRUE, UINT64_MAX);

    bool ok = false;
    void* mapped = nullptr;
    if (r->vk.MapMemory(r->device, cap.memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
        size_t px_count = (size_t)cap.width * cap.height;
        uint8_t* dst = (uint8_t*)mem_alloc(alloc, px_count * 4, MEM_DEFAULT_ALIGN);
        if (dst) {
            const uint8_t* src = (const uint8_t*)mapped;
            if (rgba) {
                memcpy(dst, src, px_count * 4);
            } else {   // BGRA -> RGBA swizzle
                for (size_t p = 0; p < px_count; ++p) {
                    dst[p * 4 + 0] = src[p * 4 + 2];
                    dst[p * 4 + 1] = src[p * 4 + 1];
                    dst[p * 4 + 2] = src[p * 4 + 0];
                    dst[p * 4 + 3] = src[p * 4 + 3];
                }
            }
            out->width  = (int)cap.width;
            out->height = (int)cap.height;
            out->rgba8  = dst;
            ok = true;
        }
        r->vk.UnmapMemory(r->device, cap.memory);
    }
    free_buffer(r, &cap.buffer, &cap.memory);
    return ok;
}

static bool resolve_draw(void* user, const DrawItem* item, uint32_t* pipeline_key) {
    Renderer* r = (Renderer*)user;
    if (!r || !item || !pipeline_key ||
        !render_handle_valid(&r->mesh_handles, item->mesh.h) ||
        !render_handle_valid(&r->material_handles, item->material.h)) return false;
    const MaterialResource* material = &r->materials[handle_index(item->material.h)];
    if (!render_handle_valid(&r->texture_handles, material->texture.h)) return false;
    *pipeline_key = PIPELINE_MESH;
    return true;
}

bool renderer_begin_frame(Renderer* r, const FrameView* view,
                          int fb_width, int fb_height, bool minimized) {
    if (!r || !view || r->frame_begun) return false;
    Arena* arena = &r->frame_arenas[r->frame];
    arena_reset(arena);
    r->draw_items = ARENA_PUSH_ARRAY(arena, DrawItem, MAX_DRAW_ITEMS);
    if (!r->draw_items || !render_debug_begin(&r->debug_draw, arena, MAX_DEBUG_VERTS)) return false;
    r->draw_count = 0;
    r->batch_output = {};
    r->instance_cpu = nullptr;
    r->instance_count = 0;
    r->debug_overlay_started = false;
    r->camera_ubo.view = view->view;
    r->camera_ubo.proj = view->proj;
    r->pending_fb_width = fb_width;
    r->pending_fb_height = fb_height;
    r->pending_minimized = minimized;
    r->frame_begun = true;
    return true;
}

bool renderer_submit(Renderer* r, const DrawItem* items, uint32_t count) {
    if (!r || !r->frame_begun ||
        !render_submission_preflight(items, count, r->draw_count, MAX_DRAW_ITEMS))
        return false;
    if (count > 0) memcpy(r->draw_items + r->draw_count, items, sizeof(DrawItem) * count);
    r->draw_count += count;
    return true;
}

static bool prepare_submitted_frame(Renderer* r) {
    if (!r || !r->frame_begun) return false;
    Arena* arena = &r->frame_arenas[r->frame];
    if (!render_build_batches(r->draw_items, r->draw_count, MAX_DRAW_ITEMS, arena,
                              resolve_draw, r, &r->batch_output)) {
        platform_log("renderer: rejected invalid or over-capacity draw submission\n");
        r->frame_begun = false;
        return false;
    }
    r->instance_cpu = r->batch_output.instances;
    r->instance_count = r->batch_output.instance_count;
    if (!r->debug_overlay_started) render_debug_end_world(&r->debug_draw);
    r->stats.submitted_objects = r->draw_count;
    r->stats.scene_batches = r->batch_output.batch_count;
    r->stats.frame_arena_bytes = arena->offset;
    r->stats.frame_arena_high_water = arena->high_water;
    r->stats.persistent_arena_bytes = r->arena.offset;
    r->stats.persistent_arena_high_water = r->arena.high_water;
    r->stats.live_device_allocations = r->alloc_count;
    r->frame_begun = false;
    return true;
}

void renderer_end_frame(Renderer* r) {
    if (!prepare_submitted_frame(r)) return;
    draw_frame(r, r->pending_fb_width, r->pending_fb_height, r->pending_minimized, nullptr);
}

bool renderer_end_frame_capture(Renderer* r, Allocator alloc, RendererCapture* out) {
    if (!prepare_submitted_frame(r)) return false;
    return capture_frame(r, r->pending_fb_width, r->pending_fb_height,
                         r->pending_minimized, alloc, out);
}

RendererStats renderer_get_stats(const Renderer* r) {
    return r ? r->stats : RendererStats{};
}

void dbg_line(Renderer* r, mm::vec3 a, mm::vec3 b, uint32_t color) {
    if (r && r->frame_begun && !r->debug_overlay_started)
        render_debug_line(&r->debug_draw, a, b, color);
}

void dbg_sphere(Renderer* r, mm::vec3 center, float radius, uint32_t color) {
    if (r && r->frame_begun && !r->debug_overlay_started)
        render_debug_sphere(&r->debug_draw, center, radius, color, 24);
}

void dbg_aabb(Renderer* r, mm::vec3 lo, mm::vec3 hi, uint32_t color) {
    if (r && r->frame_begun && !r->debug_overlay_started)
        render_debug_aabb(&r->debug_draw, lo, hi, color);
}

void dbg_text_2d(Renderer* r, float x, float y, float scale, uint32_t color, const char* text) {
    if (!r || !r->frame_begun) return;
    if (!r->debug_overlay_started) {
        render_debug_end_world(&r->debug_draw);
        r->debug_overlay_started = true;
    }
    render_debug_text_2d(&r->debug_draw, x, y, scale, color, text);
}
