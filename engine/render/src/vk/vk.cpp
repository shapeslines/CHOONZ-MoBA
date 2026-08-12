#include "vk/vk.h"
#include "platform/platform.h"   // platform_fatal
#include <cstring>

// Resolve a proc or die loudly at load time (ADR-0004): a mis-resolved pointer should
// fail here with a clear message, never crash opaquely mid-frame.
#define VK_GLOBAL(field, name) do {                                              \
        vk->field = (PFN_##name)vk->GetInstanceProcAddr(nullptr, #name);         \
        if (!vk->field) platform_fatal("vk: global proc %s not found\n", #name);  \
    } while (0)

#define VK_INSTANCE(field, name) do {                                            \
        vk->field = (PFN_##name)vk->GetInstanceProcAddr(instance, #name);        \
        if (!vk->field) platform_fatal("vk: instance proc %s not found\n", #name); \
    } while (0)

#define VK_DEVICE(field, name) do {                                              \
        vk->field = (PFN_##name)vk->GetDeviceProcAddr(device, #name);            \
        if (!vk->field) platform_fatal("vk: device proc %s not found\n", #name);   \
    } while (0)

bool vk_load_global(Vk* vk, PFN_vkGetInstanceProcAddr gipa) {
    memset(vk, 0, sizeof(*vk));
    vk->GetInstanceProcAddr = gipa;
    VK_GLOBAL(CreateInstance,                       vkCreateInstance);
    VK_GLOBAL(EnumerateInstanceLayerProperties,     vkEnumerateInstanceLayerProperties);
    VK_GLOBAL(EnumerateInstanceExtensionProperties, vkEnumerateInstanceExtensionProperties);
    return true;
}

bool vk_load_instance(Vk* vk, VkInstance instance) {
    VK_INSTANCE(GetDeviceProcAddr,                       vkGetDeviceProcAddr);
    VK_INSTANCE(DestroyInstance,                         vkDestroyInstance);
    VK_INSTANCE(EnumeratePhysicalDevices,                vkEnumeratePhysicalDevices);
    VK_INSTANCE(GetPhysicalDeviceProperties,             vkGetPhysicalDeviceProperties);
    VK_INSTANCE(GetPhysicalDeviceFeatures2,              vkGetPhysicalDeviceFeatures2);
    VK_INSTANCE(GetPhysicalDeviceFormatProperties,       vkGetPhysicalDeviceFormatProperties);
    VK_INSTANCE(GetPhysicalDeviceMemoryProperties,       vkGetPhysicalDeviceMemoryProperties);
    VK_INSTANCE(GetPhysicalDeviceQueueFamilyProperties,  vkGetPhysicalDeviceQueueFamilyProperties);
    VK_INSTANCE(EnumerateDeviceExtensionProperties,      vkEnumerateDeviceExtensionProperties);
    VK_INSTANCE(CreateDevice,                            vkCreateDevice);
    VK_INSTANCE(DestroySurfaceKHR,                       vkDestroySurfaceKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceSupportKHR,      vkGetPhysicalDeviceSurfaceSupportKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR,      vkGetPhysicalDeviceSurfaceFormatsKHR);
    VK_INSTANCE(GetPhysicalDeviceSurfacePresentModesKHR, vkGetPhysicalDeviceSurfacePresentModesKHR);
    // VK_EXT_debug_utils is present only when the extension was enabled — nullable.
    vk->CreateDebugUtilsMessengerEXT  = (PFN_vkCreateDebugUtilsMessengerEXT) vk->GetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    vk->DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vk->GetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    return true;
}

bool vk_load_device(Vk* vk, VkDevice device) {
    VK_DEVICE(DestroyDevice,          vkDestroyDevice);
    VK_DEVICE(GetDeviceQueue,         vkGetDeviceQueue);
    VK_DEVICE(DeviceWaitIdle,         vkDeviceWaitIdle);
    VK_DEVICE(CreateSwapchainKHR,     vkCreateSwapchainKHR);
    VK_DEVICE(DestroySwapchainKHR,    vkDestroySwapchainKHR);
    VK_DEVICE(GetSwapchainImagesKHR,  vkGetSwapchainImagesKHR);
    VK_DEVICE(AcquireNextImageKHR,    vkAcquireNextImageKHR);
    VK_DEVICE(QueuePresentKHR,        vkQueuePresentKHR);
    VK_DEVICE(CreateSemaphore,        vkCreateSemaphore);
    VK_DEVICE(DestroySemaphore,       vkDestroySemaphore);
    VK_DEVICE(CreateFence,            vkCreateFence);
    VK_DEVICE(DestroyFence,           vkDestroyFence);
    VK_DEVICE(WaitForFences,          vkWaitForFences);
    VK_DEVICE(ResetFences,            vkResetFences);
    VK_DEVICE(CreateCommandPool,      vkCreateCommandPool);
    VK_DEVICE(DestroyCommandPool,     vkDestroyCommandPool);
    VK_DEVICE(AllocateCommandBuffers, vkAllocateCommandBuffers);
    VK_DEVICE(BeginCommandBuffer,     vkBeginCommandBuffer);
    VK_DEVICE(EndCommandBuffer,       vkEndCommandBuffer);
    VK_DEVICE(ResetCommandBuffer,     vkResetCommandBuffer);
    VK_DEVICE(QueueSubmit,            vkQueueSubmit);
    VK_DEVICE(CmdPipelineBarrier,     vkCmdPipelineBarrier);
    VK_DEVICE(CmdClearColorImage,     vkCmdClearColorImage);

    // M2.1: pipeline + dynamic rendering + synchronization2 (core 1.3, gated by
    // ADR-0012 at device selection, so a missing proc here is a real loader bug).
    VK_DEVICE(CreateShaderModule,      vkCreateShaderModule);
    VK_DEVICE(DestroyShaderModule,     vkDestroyShaderModule);
    VK_DEVICE(CreatePipelineLayout,    vkCreatePipelineLayout);
    VK_DEVICE(DestroyPipelineLayout,   vkDestroyPipelineLayout);
    VK_DEVICE(CreateGraphicsPipelines, vkCreateGraphicsPipelines);
    VK_DEVICE(DestroyPipeline,         vkDestroyPipeline);
    VK_DEVICE(CreatePipelineCache,     vkCreatePipelineCache);
    VK_DEVICE(DestroyPipelineCache,    vkDestroyPipelineCache);
    VK_DEVICE(GetPipelineCacheData,    vkGetPipelineCacheData);
    VK_DEVICE(CreateImageView,         vkCreateImageView);
    VK_DEVICE(DestroyImageView,        vkDestroyImageView);
    VK_DEVICE(CmdBeginRendering,       vkCmdBeginRendering);
    VK_DEVICE(CmdEndRendering,         vkCmdEndRendering);
    VK_DEVICE(CmdBindPipeline,         vkCmdBindPipeline);
    VK_DEVICE(CmdSetViewport,          vkCmdSetViewport);
    VK_DEVICE(CmdSetScissor,           vkCmdSetScissor);
    VK_DEVICE(CmdDraw,                 vkCmdDraw);
    VK_DEVICE(QueueSubmit2,            vkQueueSubmit2);
    VK_DEVICE(CmdPipelineBarrier2,     vkCmdPipelineBarrier2);

    // M2.1: readback screenshot staging.
    VK_DEVICE(CreateBuffer,                vkCreateBuffer);
    VK_DEVICE(DestroyBuffer,               vkDestroyBuffer);
    VK_DEVICE(GetBufferMemoryRequirements, vkGetBufferMemoryRequirements);
    VK_DEVICE(AllocateMemory,              vkAllocateMemory);
    VK_DEVICE(FreeMemory,                  vkFreeMemory);
    VK_DEVICE(BindBufferMemory,            vkBindBufferMemory);
    VK_DEVICE(MapMemory,                   vkMapMemory);
    VK_DEVICE(UnmapMemory,                 vkUnmapMemory);
    VK_DEVICE(CmdCopyImageToBuffer,        vkCmdCopyImageToBuffer);

    // M2.2: textured quad.
    VK_DEVICE(CreateImage,                vkCreateImage);
    VK_DEVICE(DestroyImage,               vkDestroyImage);
    VK_DEVICE(GetImageMemoryRequirements, vkGetImageMemoryRequirements);
    VK_DEVICE(BindImageMemory,            vkBindImageMemory);
    VK_DEVICE(CreateSampler,              vkCreateSampler);
    VK_DEVICE(DestroySampler,             vkDestroySampler);
    VK_DEVICE(CreateDescriptorSetLayout,  vkCreateDescriptorSetLayout);
    VK_DEVICE(DestroyDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    VK_DEVICE(CreateDescriptorPool,       vkCreateDescriptorPool);
    VK_DEVICE(DestroyDescriptorPool,      vkDestroyDescriptorPool);
    VK_DEVICE(AllocateDescriptorSets,     vkAllocateDescriptorSets);
    VK_DEVICE(UpdateDescriptorSets,       vkUpdateDescriptorSets);
    VK_DEVICE(FreeCommandBuffers,         vkFreeCommandBuffers);
    VK_DEVICE(CmdCopyBuffer,              vkCmdCopyBuffer);
    VK_DEVICE(CmdCopyBufferToImage,       vkCmdCopyBufferToImage);
    VK_DEVICE(CmdBindVertexBuffers,       vkCmdBindVertexBuffers);
    VK_DEVICE(CmdBindIndexBuffer,         vkCmdBindIndexBuffer);
    VK_DEVICE(CmdBindDescriptorSets,      vkCmdBindDescriptorSets);
    VK_DEVICE(CmdPushConstants,           vkCmdPushConstants);
    VK_DEVICE(CmdDrawIndexed,             vkCmdDrawIndexed);
    return true;
}
