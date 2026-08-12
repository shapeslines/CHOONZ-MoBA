#pragma once
// The hand-loaded Vulkan dispatch table (ADR-0004). <vulkan/vulkan.h> is confined to
// this file (+ vk.cpp / renderer.cpp). Every Vulkan call goes through `Vk` (vk.CreateX)
// — never a presumed-linked vkCreateX — and each pointer is null-checked at load time
// so a mis-resolved entry fails loudly, not mid-frame.

// VK_NO_PROTOTYPES: do not declare the vk* prototypes (they'd want vulkan-1.lib at link
// time; we resolve every pointer ourselves). WIN32 surface types come in via the define.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4255)   // () -> (void) in Windows headers pulled by vulkan_win32.h
#endif
#include <vulkan/vulkan.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

// The dispatch table, filled in tiers. Names drop the vk prefix: vk.CreateInstance(...).
struct Vk {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;

    // --- global tier (resolved against a NULL instance) ---
    PFN_vkCreateInstance                       CreateInstance;
    PFN_vkEnumerateInstanceLayerProperties     EnumerateInstanceLayerProperties;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;

    // --- instance tier (resolved after vkCreateInstance) ---
    PFN_vkGetDeviceProcAddr                          GetDeviceProcAddr;
    PFN_vkDestroyInstance                            DestroyInstance;
    PFN_vkEnumeratePhysicalDevices                   EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties                GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceFeatures2                 GetPhysicalDeviceFeatures2;        // 1.3 feature gate (ADR-0012)
    PFN_vkGetPhysicalDeviceFormatProperties          GetPhysicalDeviceFormatProperties; // depth-format selection
    PFN_vkGetPhysicalDeviceMemoryProperties          GetPhysicalDeviceMemoryProperties; // readback staging
    PFN_vkGetPhysicalDeviceQueueFamilyProperties     GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkEnumerateDeviceExtensionProperties         EnumerateDeviceExtensionProperties;
    PFN_vkCreateDevice                               CreateDevice;
    PFN_vkDestroySurfaceKHR                          DestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR         GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR    GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR         GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR    GetPhysicalDeviceSurfacePresentModesKHR;
    // VK_EXT_debug_utils (optional — only if the extension was enabled)
    PFN_vkCreateDebugUtilsMessengerEXT  CreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT;

    // --- device tier (resolved via vkGetDeviceProcAddr — skips the loader trampoline) ---
    PFN_vkDestroyDevice          DestroyDevice;
    PFN_vkGetDeviceQueue         GetDeviceQueue;
    PFN_vkDeviceWaitIdle         DeviceWaitIdle;
    PFN_vkCreateSwapchainKHR     CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR    DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR  GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR    AcquireNextImageKHR;
    PFN_vkQueuePresentKHR        QueuePresentKHR;
    PFN_vkCreateSemaphore        CreateSemaphore;
    PFN_vkDestroySemaphore       DestroySemaphore;
    PFN_vkCreateFence            CreateFence;
    PFN_vkDestroyFence           DestroyFence;
    PFN_vkWaitForFences          WaitForFences;
    PFN_vkResetFences            ResetFences;
    PFN_vkCreateCommandPool      CreateCommandPool;
    PFN_vkDestroyCommandPool     DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer     BeginCommandBuffer;
    PFN_vkEndCommandBuffer       EndCommandBuffer;
    PFN_vkResetCommandBuffer     ResetCommandBuffer;
    PFN_vkQueueSubmit            QueueSubmit;
    PFN_vkCmdPipelineBarrier     CmdPipelineBarrier;
    PFN_vkCmdClearColorImage     CmdClearColorImage;

    // M2.1: pipeline + dynamic rendering + synchronization2 (all core 1.3 — required
    // by the ADR-0012 minimum spec, so resolved fatally like the rest of the tier).
    PFN_vkCreateShaderModule        CreateShaderModule;
    PFN_vkDestroyShaderModule       DestroyShaderModule;
    PFN_vkCreatePipelineLayout      CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout     DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines   CreateGraphicsPipelines;
    PFN_vkDestroyPipeline           DestroyPipeline;
    PFN_vkCreatePipelineCache       CreatePipelineCache;
    PFN_vkDestroyPipelineCache      DestroyPipelineCache;
    PFN_vkGetPipelineCacheData      GetPipelineCacheData;
    PFN_vkCreateImageView           CreateImageView;
    PFN_vkDestroyImageView          DestroyImageView;
    PFN_vkCmdBeginRendering         CmdBeginRendering;
    PFN_vkCmdEndRendering           CmdEndRendering;
    PFN_vkCmdBindPipeline           CmdBindPipeline;
    PFN_vkCmdSetViewport            CmdSetViewport;
    PFN_vkCmdSetScissor             CmdSetScissor;
    PFN_vkCmdDraw                   CmdDraw;
    PFN_vkQueueSubmit2              QueueSubmit2;
    PFN_vkCmdPipelineBarrier2       CmdPipelineBarrier2;

    // M2.1: readback screenshot (renderer_capture) — buffer + dedicated allocation.
    PFN_vkCreateBuffer                CreateBuffer;
    PFN_vkDestroyBuffer               DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory              AllocateMemory;
    PFN_vkFreeMemory                  FreeMemory;
    PFN_vkBindBufferMemory            BindBufferMemory;
    PFN_vkMapMemory                   MapMemory;
    PFN_vkUnmapMemory                 UnmapMemory;
    PFN_vkCmdCopyImageToBuffer        CmdCopyImageToBuffer;

    // M2.2: textured quad — images, samplers, descriptors, staging copies, indexed draw.
    PFN_vkCreateImage                 CreateImage;
    PFN_vkDestroyImage                DestroyImage;
    PFN_vkGetImageMemoryRequirements  GetImageMemoryRequirements;
    PFN_vkBindImageMemory             BindImageMemory;
    PFN_vkCreateSampler               CreateSampler;
    PFN_vkDestroySampler              DestroySampler;
    PFN_vkCreateDescriptorSetLayout   CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout  DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool        CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool       DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets      AllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets        UpdateDescriptorSets;
    PFN_vkFreeCommandBuffers          FreeCommandBuffers;
    PFN_vkCmdCopyBuffer               CmdCopyBuffer;
    PFN_vkCmdCopyBufferToImage        CmdCopyBufferToImage;
    PFN_vkCmdBindVertexBuffers        CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer          CmdBindIndexBuffer;
    PFN_vkCmdBindDescriptorSets       CmdBindDescriptorSets;
    PFN_vkCmdPushConstants            CmdPushConstants;
    PFN_vkCmdDrawIndexed              CmdDrawIndexed;
};

// Resolve the global tier from the platform loader. Fatal on a missing core proc.
bool vk_load_global(Vk* vk, PFN_vkGetInstanceProcAddr gipa);
// Resolve the instance tier. Fatal on a missing core proc; debug-utils stay nullable.
bool vk_load_instance(Vk* vk, VkInstance instance);
// Resolve the device tier (after vkCreateDevice). Fatal on a missing proc.
bool vk_load_device(Vk* vk, VkDevice device);
