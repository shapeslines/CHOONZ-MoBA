#include "render/renderer.h"
#include "render/pipeline_cache_check.h"
#include "vk/vk.h"
#include "platform/platform.h"          // platform_log / platform_fatal / file I/O / arena
#include "platform/platform_vulkan.h"   // platform_vk_get_loader / platform_vk_create_surface
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

// M2.2: first real GPU memory traffic on top of the M2.1 pipeline path. The frame is
// rendered with DYNAMIC RENDERING + SYNCHRONIZATION2 (ADR-0012): barrier2 to
// COLOR_ATTACHMENT_OPTIMAL -> vkCmdBeginRendering (loadOp=CLEAR carries the animated
// color) -> triangle -> textured quad (VB/IB + set=1 sampler descriptor) -> barrier2
// to PRESENT_SRC -> QueueSubmit2. Buffers/images are DEVICE_LOCAL, filled through
// HOST_COHERENT staging + one-shot submits at startup. Frame pacing is unchanged from
// M2.0: frames-in-flight = 2, per-frame command buffer + image_available semaphore +
// in_flight fence, per-SWAPCHAIN-IMAGE render_finished semaphore + images_in_flight
// fence pointer.
#define FRAMES_IN_FLIGHT 2
#define MAX_SC_IMAGES    8

// ---- Static pipeline registry (roadmap M2.1) ----------------------------------
// Pipelines the renderer owns, created at startup from offline-compiled SPIR-V
// (ADR-0008: glslc -> ${build}/shaders, located via MOBA_SHADER_DIR).
enum PipelineVertexLayout {
    VERTEX_LAYOUT_NONE,        // verts hardcoded in the shader (M2.1 triangle)
    VERTEX_LAYOUT_POS2_UV2,    // QuadVertex: vec2 pos + vec2 uv from a vertex buffer
};
enum PipelineLayoutKind {
    LAYOUT_EMPTY,              // no descriptors (triangle)
    LAYOUT_MATERIAL,           // set=0 empty placeholder + set=1 texture+sampler
};
struct PipelineDesc {
    const char* name;
    const char* vert_spv;      // file name inside MOBA_SHADER_DIR
    const char* frag_spv;
    PipelineVertexLayout vertex_layout;
    PipelineLayoutKind   layout_kind;
};
enum { PIPELINE_TRIANGLE = 0, PIPELINE_QUAD = 1, PIPELINE_COUNT = 2 };
static const PipelineDesc k_pipeline_registry[PIPELINE_COUNT] = {
    { "triangle", "triangle.vert.spv", "triangle.frag.spv", VERTEX_LAYOUT_NONE,     LAYOUT_EMPTY    },
    { "quad",     "quad.vert.spv",     "quad.frag.spv",     VERTEX_LAYOUT_POS2_UV2, LAYOUT_MATERIAL },
};

// ---- Quad geometry (M2.2/M2.3) ----------------------------------------------------
// NDC is Y-down; uv (0,0) is the texture's top-left texel (rows uploaded top-down),
// so the top-left vertex carries uv (0,0) and the image appears upright. Since M2.3
// the verts are WORLD-space XY (z=0) on the ground plane around the origin, warped by
// the camera UBO — ±2.0 units reads well from the sandbox camera's default distance.
struct QuadVertex { float x, y, u, v; };
static const QuadVertex k_quad_verts[4] = {
    { -2.0f, -2.0f, 0.0f, 0.0f },   // top-left
    {  2.0f, -2.0f, 1.0f, 0.0f },   // top-right
    {  2.0f,  2.0f, 1.0f, 1.0f },   // bottom-right
    { -2.0f,  2.0f, 0.0f, 1.0f },   // bottom-left
};
static const uint16_t k_quad_indices[6] = { 0, 1, 2, 0, 2, 3 };

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
    VkSemaphore    render_finished[MAX_SC_IMAGES];   // per image
    VkFence        images_in_flight[MAX_SC_IMAGES];  // borrowed frame fence, or NULL
    bool           need_recreate;
    bool           sc_can_transfer_src;              // capture/readback possible

    // pipelines (M2.1)
    VkPipelineCache  pipeline_cache;
    VkPipelineLayout pipeline_layout;                // empty (triangle)
    VkPipeline       pipelines[PIPELINE_COUNT];
    VkFormat         pipelines_format;               // sc format they were built for

    // vk_alloc Phase 1 (M2.2): dedicated allocations, counted against the cap.
    uint32_t         alloc_count;

    // textured quad (M2.2)
    VkBuffer              quad_vb,  quad_ib;
    VkDeviceMemory        quad_vb_mem, quad_ib_mem;
    VkImage               texture;
    VkDeviceMemory        texture_mem;
    VkImageView           texture_view;
    VkSampler             sampler;
    VkDescriptorSetLayout set0_layout;               // set=0: per-frame view/proj UBO (M2.3)
    VkDescriptorSetLayout material_layout;           // set=1: combined image sampler
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       material_set;
    VkPipelineLayout      material_pipeline_layout;  // [set0_layout, material_layout]
    bool                  texture_ready;             // quad draws only once this is set

    // camera / per-frame UBO ring (M2.3): one persistently-mapped HOST_COHERENT buffer
    // + descriptor per frame slot. Written at the top of draw_frame — slot fr is safe
    // to touch because in_flight[fr] was just waited on (the old frame is done).
    struct CameraUBO { mm::mat4 view; mm::mat4 proj; };   // 128 B, 16-aligned, std140-clean
    CameraUBO             camera_ubo;                 // latest app camera (identity until set)
    VkBuffer              ubo[FRAMES_IN_FLIGHT];
    VkDeviceMemory        ubo_mem[FRAMES_IN_FLIGHT];
    void*                 ubo_map[FRAMES_IN_FLIGHT];
    VkDescriptorSet       ubo_set[FRAMES_IN_FLIGHT];

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
    return true;
}

static void free_image(Renderer* r, VkImage* img, VkDeviceMemory* mem) {
    if (*img) { r->vk.DestroyImage(r->device, *img, nullptr); *img = VK_NULL_HANDLE; }
    if (*mem) { r->vk.FreeMemory(r->device, *mem, nullptr); *mem = VK_NULL_HANDLE; --r->alloc_count; }
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
    snprintf(path, sizeof(path), "%s/%s", MOBA_SHADER_DIR, spv_name);

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

        // Vertex input per registry entry: the triangle keeps its empty input (verts
        // hardcoded in the shader, M2.1); the quad streams QuadVertex (M2.2).
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = sizeof(QuadVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;                       // pos
        attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = sizeof(float) * 2;       // uv
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (d->vertex_layout == VERTEX_LAYOUT_POS2_UV2) {
            vin.vertexBindingDescriptionCount   = 1;
            vin.pVertexBindingDescriptions      = &bind;
            vin.vertexAttributeDescriptionCount = 2;
            vin.pVertexAttributeDescriptions    = attrs;
        }

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;     // pointers null: dynamic state below
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;    // first triangle: visible no matter the winding
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
        ci.pColorBlendState    = &blend;
        ci.pDynamicState       = &dyn;
        ci.layout              = d->layout_kind == LAYOUT_MATERIAL ? r->material_pipeline_layout : r->pipeline_layout;
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
    // TRANSFER_SRC is added when the surface allows it so renderer_capture can read
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

    if (r->vk.CreateInstance(&ci, nullptr, &r->instance) != VK_SUCCESS) {
        platform_log("renderer: vkCreateInstance failed\n");
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

    // Pipeline layouts. The triangle's stays empty; the quad's is [set=0 empty
    // placeholder, set=1 material] so M2.3's per-frame UBO can claim set=0 without
    // renumbering anything (roadmap: material lives at set=1).
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (r->vk.CreatePipelineLayout(r->device, &plci, nullptr, &r->pipeline_layout) != VK_SUCCESS) {
        platform_log("renderer: vkCreatePipelineLayout failed\n"); renderer_destroy(r); return nullptr;
    }
    {
        // set=0: per-frame view/proj UBO (M2.3) — the slot M2.2 reserved empty. One
        // binding: uniform buffer at 0, vertex stage only. The triangle's own layout
        // stays truly empty (it has no descriptors); only the material pipeline
        // carries [set0_layout, material_layout].
        VkDescriptorSetLayoutBinding ubo_bind{};
        ubo_bind.binding         = 0;
        ubo_bind.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_bind.descriptorCount = 1;
        ubo_bind.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo set0_ci{};
        set0_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0_ci.bindingCount = 1;
        set0_ci.pBindings    = &ubo_bind;

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
        VkDescriptorSetLayout sets[2] = { r->set0_layout, r->material_layout };
        VkPipelineLayoutCreateInfo mat_plci{};
        mat_plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        mat_plci.setLayoutCount = 2;
        mat_plci.pSetLayouts    = sets;
        if (r->vk.CreatePipelineLayout(r->device, &mat_plci, nullptr, &r->material_pipeline_layout) != VK_SUCCESS) {
            platform_log("renderer: material pipeline layout failed\n"); renderer_destroy(r); return nullptr;
        }

        // One pool for the whole descriptor life: FRAMES_IN_FLIGHT UBO sets (set=0) +
        // one material set (set=1). Sized up front; no per-frame allocation.
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = FRAMES_IN_FLIGHT;
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets       = FRAMES_IN_FLIGHT + 1;
        pool_ci.poolSizeCount = 2;
        pool_ci.pPoolSizes    = pool_sizes;
        VkDescriptorSetAllocateInfo set_ai{};
        if (r->vk.CreateDescriptorPool(r->device, &pool_ci, nullptr, &r->desc_pool) != VK_SUCCESS) {
            platform_log("renderer: vkCreateDescriptorPool failed\n"); renderer_destroy(r); return nullptr;
        }
        set_ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_ai.descriptorPool     = r->desc_pool;
        set_ai.descriptorSetCount = 1;
        set_ai.pSetLayouts        = &r->material_layout;
        if (r->vk.AllocateDescriptorSets(r->device, &set_ai, &r->material_set) != VK_SUCCESS) {
            platform_log("renderer: vkAllocateDescriptorSets failed\n"); renderer_destroy(r); return nullptr;
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
        VkDescriptorBufferInfo binfo{};
        binfo.range = ubo_bytes;
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            if (!alloc_buffer(r, ubo_bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &r->ubo[i], &r->ubo_mem[i])) {
                platform_log("renderer: per-frame UBO alloc failed\n"); renderer_destroy(r); return nullptr;
            }
            if (r->vk.MapMemory(r->device, r->ubo_mem[i], 0, ubo_bytes, 0, &r->ubo_map[i]) != VK_SUCCESS) {
                platform_log("renderer: UBO map failed\n"); renderer_destroy(r); return nullptr;
            }
            VkDescriptorSetAllocateInfo set_ai{};
            set_ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            set_ai.descriptorPool     = r->desc_pool;
            set_ai.descriptorSetCount = 1;
            set_ai.pSetLayouts        = &r->set0_layout;
            if (r->vk.AllocateDescriptorSets(r->device, &set_ai, &r->ubo_set[i]) != VK_SUCCESS) {
                platform_log("renderer: UBO descriptor set alloc failed\n"); renderer_destroy(r); return nullptr;
            }
            binfo.buffer = r->ubo[i];
            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = r->ubo_set[i];
            write.dstBinding      = 0;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo     = &binfo;
            r->vk.UpdateDescriptorSets(r->device, 1, &write, 0, nullptr);
        }
        // Identity until the app hands a camera in (quad then draws in raw NDC).
        r->camera_ubo.view = mm::mat4{};
        r->camera_ubo.view.m[0][0] = 1; r->camera_ubo.view.m[1][1] = 1;
        r->camera_ubo.view.m[2][2] = 1; r->camera_ubo.view.m[3][3] = 1;
        r->camera_ubo.proj = r->camera_ubo.view;
    }

    // Quad VB/IB: DEVICE_LOCAL, filled through ONE staging buffer (verts then indices)
    // and a one-shot copy. The one-shot ends with buffer barriers2 into
    // VERTEX_ATTRIBUTE_INPUT/INDEX_INPUT so every later frame submission sees the data
    // (the fence only tells the HOST it finished).
    {
        const VkDeviceSize vb_bytes = sizeof(k_quad_verts);
        const VkDeviceSize ib_bytes = sizeof(k_quad_indices);
        VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        bool ok = alloc_buffer(r, vb_bytes + ib_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging, &staging_mem) &&
                  alloc_buffer(r, vb_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->quad_vb, &r->quad_vb_mem) &&
                  alloc_buffer(r, ib_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->quad_ib, &r->quad_ib_mem);
        void* mapped = nullptr;
        if (ok) ok = r->vk.MapMemory(r->device, staging_mem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
        if (ok) {
            memcpy(mapped, k_quad_verts, (size_t)vb_bytes);
            memcpy((uint8_t*)mapped + vb_bytes, k_quad_indices, (size_t)ib_bytes);
            r->vk.UnmapMemory(r->device, staging_mem);   // HOST_COHERENT: no flush needed

            VkCommandBuffer cb = begin_one_shot(r);
            ok = cb != VK_NULL_HANDLE;
            if (ok) {
                VkBufferCopy vb_copy{}; vb_copy.srcOffset = 0;        vb_copy.size = vb_bytes;
                VkBufferCopy ib_copy{}; ib_copy.srcOffset = vb_bytes; ib_copy.size = ib_bytes;
                r->vk.CmdCopyBuffer(cb, staging, r->quad_vb, 1, &vb_copy);
                r->vk.CmdCopyBuffer(cb, staging, r->quad_ib, 1, &ib_copy);

                VkBufferMemoryBarrier2 bb[2]{};
                bb[0].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                bb[0].srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
                bb[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                bb[0].dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
                bb[0].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
                bb[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bb[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bb[0].buffer        = r->quad_vb;
                bb[0].size          = VK_WHOLE_SIZE;
                bb[1] = bb[0];
                bb[1].dstStageMask  = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
                bb[1].dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
                bb[1].buffer        = r->quad_ib;
                VkDependencyInfo dep{};
                dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.bufferMemoryBarrierCount = 2;
                dep.pBufferMemoryBarriers    = bb;
                r->vk.CmdPipelineBarrier2(cb, &dep);
                ok = end_one_shot(r, cb);
            }
        }
        free_buffer(r, &staging, &staging_mem);
        if (!ok) {
            platform_log("renderer: quad vertex/index upload failed\n");
            renderer_destroy(r);
            return nullptr;
        }
    }

    create_pipeline_cache(r);

    int32_t w = 0, h = 0; platform_window_size(window, &w, &h);
    create_swapchain(r, (uint32_t)(w > 0 ? w : 1), (uint32_t)(h > 0 ? h : 1));

    // Pipelines need the swapchain format; if the window started minimized the build
    // is deferred to the first successful (re)create inside renderer_draw.
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

// M2.2 (provisional seam — unified into typed handles at M2.5): staging upload of the
// quad's texture. Blocking by design; this is a startup path.
bool renderer_upload_texture(Renderer* r, int width, int height, const void* rgba8) {
    if (!r || !rgba8 || width <= 0 || height <= 0) return false;
    // vkCreateImage with extent > maxImageDimension2D is invalid usage (UB), not a
    // graceful error — the app layer can't query the limit through the seam, so the
    // guarantee that bad input yields false-(logged) has to live here.
    const uint32_t max_dim = r->props.limits.maxImageDimension2D;
    if ((uint32_t)width > max_dim || (uint32_t)height > max_dim) {
        platform_log("renderer: texture %dx%d exceeds maxImageDimension2D (%u)\n", width, height, max_dim);
        return false;
    }

    // Replacing an existing texture: nothing in flight may still sample it.
    if (r->texture) {
        r->vk.DeviceWaitIdle(r->device);
        r->texture_ready = false;
        if (r->texture_view) { r->vk.DestroyImageView(r->device, r->texture_view, nullptr); r->texture_view = VK_NULL_HANDLE; }
        free_image(r, &r->texture, &r->texture_mem);
    }

    const uint32_t w = (uint32_t)width, h = (uint32_t)height;
    const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    bool ok = alloc_image_2d(r, w, h, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             &r->texture, &r->texture_mem) &&
              alloc_buffer(r, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &staging, &staging_mem);
    void* mapped = nullptr;
    if (ok) ok = r->vk.MapMemory(r->device, staging_mem, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
    if (ok) {
        memcpy(mapped, rgba8, (size_t)bytes);
        r->vk.UnmapMemory(r->device, staging_mem);   // HOST_COHERENT: no flush needed

        VkCommandBuffer cb = begin_one_shot(r);
        ok = cb != VK_NULL_HANDLE;
        if (ok) {
            // UNDEFINED -> TRANSFER_DST -> copy -> SHADER_READ_ONLY (fragment sampling).
            image_barrier2(r, cb, r->texture,
                           VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { w, h, 1 };
            r->vk.CmdCopyBufferToImage(cb, staging, r->texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            image_barrier2(r, cb, r->texture,
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            ok = end_one_shot(r, cb);
        }
    }
    free_buffer(r, &staging, &staging_mem);

    if (ok) {
        VkImageViewCreateInfo vci{};
        vci.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                       = r->texture;
        vci.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                      = VK_FORMAT_R8G8B8A8_SRGB;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        ok = r->vk.CreateImageView(r->device, &vci, nullptr, &r->texture_view) == VK_SUCCESS;
    }
    if (ok) {
        VkDescriptorImageInfo img_info{};
        img_info.sampler     = r->sampler;
        img_info.imageView   = r->texture_view;
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = r->material_set;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &img_info;
        r->vk.UpdateDescriptorSets(r->device, 1, &write, 0, nullptr);
        r->texture_ready = true;
        platform_log("renderer: texture uploaded (%dx%d, sRGB) | %u dedicated allocation(s) live\n",
                     width, height, r->alloc_count);
        return true;
    }

    platform_log("renderer: texture upload failed (%dx%d)\n", width, height);
    if (r->texture_view) { r->vk.DestroyImageView(r->device, r->texture_view, nullptr); r->texture_view = VK_NULL_HANDLE; }
    free_image(r, &r->texture, &r->texture_mem);
    return false;
}

// M2.3: camera in from the app — stored once, copied into the UBO ring per frame.
void renderer_set_view_proj(Renderer* r, const mm::mat4* view, const mm::mat4* proj) {
    if (!r || !view || !proj) return;
    r->camera_ubo.view = *view;
    r->camera_ubo.proj = *proj;
}

void renderer_destroy(Renderer* r) {
    if (!r) return;
    if (r->device) r->vk.DeviceWaitIdle(r->device);
    if (r->device) save_pipeline_cache(r);   // before anything GPU-side is torn down
    destroy_pipelines(r);
    if (r->pipeline_layout) r->vk.DestroyPipelineLayout(r->device, r->pipeline_layout, nullptr);
    if (r->pipeline_cache)  r->vk.DestroyPipelineCache(r->device, r->pipeline_cache, nullptr);

    // M2.2 quad resources.
    if (r->texture_view)             r->vk.DestroyImageView(r->device, r->texture_view, nullptr);
    free_image(r, &r->texture, &r->texture_mem);
    free_buffer(r, &r->quad_vb, &r->quad_vb_mem);
    free_buffer(r, &r->quad_ib, &r->quad_ib_mem);
    // M2.3 per-frame UBO ring (descriptor sets die with the pool).
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (r->ubo_map[i]) r->vk.UnmapMemory(r->device, r->ubo_mem[i]);
        free_buffer(r, &r->ubo[i], &r->ubo_mem[i]);
    }
    if (r->sampler)                  r->vk.DestroySampler(r->device, r->sampler, nullptr);
    if (r->desc_pool)                r->vk.DestroyDescriptorPool(r->device, r->desc_pool, nullptr);   // frees material_set
    if (r->set0_layout)              r->vk.DestroyDescriptorSetLayout(r->device, r->set0_layout, nullptr);
    if (r->material_layout)          r->vk.DestroyDescriptorSetLayout(r->device, r->material_layout, nullptr);
    if (r->material_pipeline_layout) r->vk.DestroyPipelineLayout(r->device, r->material_pipeline_layout, nullptr);

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
    free(r);
}

// ---- Frame -------------------------------------------------------------------------

// Pending readback for the frame being recorded (renderer_capture's slow path).
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

    VkRenderingAttachmentInfo color{};
    color.sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView        = r->sc_views[img];
    color.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;     // the M2.0 clear lives on as loadOp
    color.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue       = clear;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent    = r->sc_extent;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color;
    r->vk.CmdBeginRendering(cb, &ri);

    VkViewport vp{};
    vp.width    = (float)r->sc_extent.width;
    vp.height   = (float)r->sc_extent.height;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = r->sc_extent;
    r->vk.CmdSetViewport(cb, 0, 1, &vp);
    r->vk.CmdSetScissor(cb, 0, 1, &scissor);

    r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_TRIANGLE]);
    r->vk.CmdDraw(cb, 3, 1, 0, 0);   // 3 verts hardcoded in triangle.vert

    if (r->texture_ready) {          // M2.2: the textured quad, painted over the triangle
        VkDeviceSize vb_offset = 0;
        r->vk.CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[PIPELINE_QUAD]);
        r->vk.CmdBindVertexBuffers(cb, 0, 1, &r->quad_vb, &vb_offset);
        r->vk.CmdBindIndexBuffer(cb, r->quad_ib, 0, VK_INDEX_TYPE_UINT16);
        // set=0 per-frame view/proj UBO (M2.3) + set=1 material — both sets at once.
        VkDescriptorSet sets[2] = { r->ubo_set[fr], r->material_set };
        r->vk.CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r->material_pipeline_layout,
                                    0, 2, sets, 0, nullptr);
        r->vk.CmdDrawIndexed(cb, 6, 1, 0, 0, 0);
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

void renderer_draw(Renderer* r, int fb_width, int fb_height, bool minimized) {
    draw_frame(r, fb_width, fb_height, minimized, nullptr);
}

bool renderer_capture(Renderer* r, int fb_width, int fb_height, bool minimized,
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
