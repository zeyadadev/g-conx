// ICD Loader Interface
// This file contains only the Vulkan loader interface functions.
// All command implementations have been moved to commands/*.cpp files.

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdNegotiateLoaderICDInterfaceVersion called\n";
    ICD_LOG_INFO() << "[Client ICD] Loader requested version: " << *pSupportedVersion << "\n";

    // Use ICD interface version 7 (latest version)
    // Version 7 adds support for additional loader features
    if (*pSupportedVersion > 7) {
        *pSupportedVersion = 7;
    }

    ICD_LOG_INFO() << "[Client ICD] Negotiated version: " << *pSupportedVersion << "\n";
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdGetInstanceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        ICD_LOG_INFO() << " -> returning nullptr\n";
        return nullptr;
    }

    // Return our implementations
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateInstanceVersion\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
    }
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateInstanceLayerProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateInstanceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkCreateInstance") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateInstance\n";
        return (PFN_vkVoidFunction)vkCreateInstance;
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetInstanceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyInstance\n";
        return (PFN_vkVoidFunction)vkDestroyInstance;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumeratePhysicalDevices\n";
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDeviceGroups") == 0 ||
        strcmp(pName, "vkEnumeratePhysicalDeviceGroupsKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumeratePhysicalDeviceGroups\n";
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDeviceGroups;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFeatures\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    }
    if (strcmp(pName, "vkCreateImageView") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateImageView\n";
        return (PFN_vkVoidFunction)vkCreateImageView;
    }
    if (strcmp(pName, "vkDestroyImageView") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyImageView\n";
        return (PFN_vkVoidFunction)vkDestroyImageView;
    }
    if (strcmp(pName, "vkCreateBufferView") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateBufferView\n";
        return (PFN_vkVoidFunction)vkCreateBufferView;
    }
    if (strcmp(pName, "vkDestroyBufferView") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyBufferView\n";
        return (PFN_vkVoidFunction)vkDestroyBufferView;
    }
    if (strcmp(pName, "vkCreateSampler") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateSampler\n";
        return (PFN_vkVoidFunction)vkCreateSampler;
    }
    if (strcmp(pName, "vkDestroySampler") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySampler\n";
        return (PFN_vkVoidFunction)vkDestroySampler;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceImageFormatProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceQueueFamilyProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceQueueFamilyProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceMemoryProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceMemoryProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFeatures2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2;
    }
    PFN_vkVoidFunction device_func = vkGetDeviceProcAddr(VK_NULL_HANDLE, pName);
    if (device_func) {
        return device_func;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (strcmp(pName, "vkCreateDevice") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateDevice\n";
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateDeviceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateDeviceLayerProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSparseImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddress") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetBufferDeviceAddress\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddress;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddressKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetBufferDeviceAddressKHR\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddressKHR;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddressEXT") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetBufferDeviceAddressEXT\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddressEXT;
    }
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (strcmp(pName, "vkCreateXcbSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateXcbSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateXcbSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceXcbPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceXcbPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceXcbPresentationSupportKHR;
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (strcmp(pName, "vkCreateXlibSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateXlibSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateXlibSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceXlibPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceXlibPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceXlibPresentationSupportKHR;
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (strcmp(pName, "vkCreateWaylandSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateWaylandSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateWaylandSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceWaylandPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceWaylandPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceWaylandPresentationSupportKHR;
    }
#endif
    if (strcmp(pName, "vkDestroySurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySurfaceKHR\n";
        return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceCapabilitiesKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceFormatsKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfacePresentModesKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
    }
    if (strcmp(pName, "vkCreateFence") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        ICD_LOG_INFO() << " -> returning vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        ICD_LOG_INFO() << " -> returning vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        ICD_LOG_INFO() << " -> returning vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueBindSparse") == 0) {
        ICD_LOG_INFO() << " -> returning vkQueueBindSparse\n";
        return (PFN_vkVoidFunction)vkQueueBindSparse;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        ICD_LOG_INFO() << " -> returning vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> returning vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> returning vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    }

    ICD_LOG_INFO() << " -> NOT FOUND, returning nullptr\n";
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdGetPhysicalDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        ICD_LOG_INFO() << " -> returning nullptr\n";
        return nullptr;
    }

    PFN_vkVoidFunction func = vk_icdGetInstanceProcAddr(instance, pName);
    if (func) {
        return func;
    }

    // Some loaders call vk_icdGetPhysicalDeviceProcAddr with device-level names.
    func = vkGetDeviceProcAddr(VK_NULL_HANDLE, pName);
    if (func) {
        return func;
    }

    ICD_LOG_INFO() << " -> Not found (nullptr)\n";
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (!pName) {
        ICD_LOG_INFO() << " -> nullptr\n";
        return nullptr;
    }

    auto device_supports_extension = [&](const char* name) -> bool {
        if (!name) {
            return false;
        }
        if (device == VK_NULL_HANDLE) {
            return true; // Allow loader queries before device creation
        }
        return g_device_state.is_extension_enabled(device, name);
    };

    auto device_api_version = [&]() -> uint32_t {
        if (device == VK_NULL_HANDLE) {
            return VK_API_VERSION_1_0;
        }
        return g_device_state.get_device_api_version(device);
    };

    // Critical: vkGetDeviceProcAddr must return itself
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }

    // Phase 3: Device-level functions
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceQueue\n";
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceQueue2\n";
        return (PFN_vkVoidFunction)vkGetDeviceQueue2;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDevice\n";
        return (PFN_vkVoidFunction)vkDestroyDevice;
    }
    if (strcmp(pName, "vkAllocateMemory") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateMemory\n";
        return (PFN_vkVoidFunction)vkAllocateMemory;
    }
    if (strcmp(pName, "vkFreeMemory") == 0) {
        ICD_LOG_INFO() << " -> vkFreeMemory\n";
        return (PFN_vkVoidFunction)vkFreeMemory;
    }
    if (strcmp(pName, "vkGetDeviceMemoryCommitment") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceMemoryCommitment\n";
        return (PFN_vkVoidFunction)vkGetDeviceMemoryCommitment;
    }
    if (strcmp(pName, "vkMapMemory") == 0) {
        ICD_LOG_INFO() << " -> vkMapMemory\n";
        return (PFN_vkVoidFunction)vkMapMemory;
    }
    if (strcmp(pName, "vkUnmapMemory") == 0) {
        ICD_LOG_INFO() << " -> vkUnmapMemory\n";
        return (PFN_vkVoidFunction)vkUnmapMemory;
    }
    if (strcmp(pName, "vkFlushMappedMemoryRanges") == 0) {
        ICD_LOG_INFO() << " -> vkFlushMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkFlushMappedMemoryRanges;
    }
    if (strcmp(pName, "vkInvalidateMappedMemoryRanges") == 0) {
        ICD_LOG_INFO() << " -> vkInvalidateMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges;
    }
    if (strcmp(pName, "vkCreateBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCreateBuffer\n";
        return (PFN_vkVoidFunction)vkCreateBuffer;
    }
    if (strcmp(pName, "vkDestroyBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyBuffer\n";
        return (PFN_vkVoidFunction)vkDestroyBuffer;
    }
    if (strcmp(pName, "vkGetBufferMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    }
    if (strcmp(pName, "vkGetBufferMemoryRequirements2") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferMemoryRequirements2\n";
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements2;
    }
    if (strcmp(pName, "vkGetBufferMemoryRequirements2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferMemoryRequirements2KHR\n";
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements2KHR;
    }
    if (strcmp(pName, "vkBindBufferMemory") == 0) {
        ICD_LOG_INFO() << " -> vkBindBufferMemory\n";
        return (PFN_vkVoidFunction)vkBindBufferMemory;
    }
    if (strcmp(pName, "vkBindBufferMemory2") == 0) {
        ICD_LOG_INFO() << " -> vkBindBufferMemory2\n";
        return (PFN_vkVoidFunction)vkBindBufferMemory2;
    }
    if (strcmp(pName, "vkBindBufferMemory2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkBindBufferMemory2KHR\n";
        return (PFN_vkVoidFunction)vkBindBufferMemory2KHR;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddress") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferDeviceAddress\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddress;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddressKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferDeviceAddressKHR\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddressKHR;
    }
    if (strcmp(pName, "vkGetBufferDeviceAddressEXT") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferDeviceAddressEXT\n";
        return (PFN_vkVoidFunction)vkGetBufferDeviceAddressEXT;
    }
    if (strcmp(pName, "vkGetBufferOpaqueCaptureAddress") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferOpaqueCaptureAddress\n";
        return (PFN_vkVoidFunction)vkGetBufferOpaqueCaptureAddress;
    }
    if (strcmp(pName, "vkGetBufferOpaqueCaptureAddressKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferOpaqueCaptureAddressKHR\n";
        return (PFN_vkVoidFunction)vkGetBufferOpaqueCaptureAddressKHR;
    }
    if (strcmp(pName, "vkGetDeviceMemoryOpaqueCaptureAddress") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceMemoryOpaqueCaptureAddress\n";
        return (PFN_vkVoidFunction)vkGetDeviceMemoryOpaqueCaptureAddress;
    }
    if (strcmp(pName, "vkGetDeviceMemoryOpaqueCaptureAddressKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceMemoryOpaqueCaptureAddressKHR\n";
        return (PFN_vkVoidFunction)vkGetDeviceMemoryOpaqueCaptureAddressKHR;
    }
    if (strcmp(pName, "vkCreateImage") == 0) {
        ICD_LOG_INFO() << " -> vkCreateImage\n";
        return (PFN_vkVoidFunction)vkCreateImage;
    }
    if (strcmp(pName, "vkDestroyImage") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyImage\n";
        return (PFN_vkVoidFunction)vkDestroyImage;
    }
    if (strcmp(pName, "vkGetImageMemoryRequirements2") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageMemoryRequirements2\n";
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements2;
    }
    if (strcmp(pName, "vkGetImageMemoryRequirements2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageMemoryRequirements2KHR\n";
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements2KHR;
    }
    if (strcmp(pName, "vkGetDeviceBufferMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceBufferMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetDeviceBufferMemoryRequirements;
    }
    if (strcmp(pName, "vkGetDeviceBufferMemoryRequirementsKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceBufferMemoryRequirementsKHR\n";
        return (PFN_vkVoidFunction)vkGetDeviceBufferMemoryRequirementsKHR;
    }
    if (strcmp(pName, "vkGetDeviceImageMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceImageMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetDeviceImageMemoryRequirements;
    }
    if (strcmp(pName, "vkGetDeviceImageMemoryRequirementsKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceImageMemoryRequirementsKHR\n";
        return (PFN_vkVoidFunction)vkGetDeviceImageMemoryRequirementsKHR;
    }
    if (strcmp(pName, "vkGetDeviceImageSparseMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceImageSparseMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetDeviceImageSparseMemoryRequirements;
    }
    if (strcmp(pName, "vkGetDeviceImageSparseMemoryRequirementsKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceImageSparseMemoryRequirementsKHR\n";
        return (PFN_vkVoidFunction)vkGetDeviceImageSparseMemoryRequirementsKHR;
    }
    if (strcmp(pName, "vkCreateImageView") == 0) {
        ICD_LOG_INFO() << " -> vkCreateImageView\n";
        return (PFN_vkVoidFunction)vkCreateImageView;
    }
    if (strcmp(pName, "vkDestroyImageView") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyImageView\n";
        return (PFN_vkVoidFunction)vkDestroyImageView;
    }
    if (strcmp(pName, "vkCreateBufferView") == 0) {
        ICD_LOG_INFO() << " -> vkCreateBufferView\n";
        return (PFN_vkVoidFunction)vkCreateBufferView;
    }
    if (strcmp(pName, "vkDestroyBufferView") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyBufferView\n";
        return (PFN_vkVoidFunction)vkDestroyBufferView;
    }
    if (strcmp(pName, "vkCreateSampler") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSampler\n";
        return (PFN_vkVoidFunction)vkCreateSampler;
    }
    if (strcmp(pName, "vkDestroySampler") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySampler\n";
        return (PFN_vkVoidFunction)vkDestroySampler;
    }
    if (strcmp(pName, "vkGetImageMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    }
    if (strcmp(pName, "vkBindImageMemory") == 0) {
        ICD_LOG_INFO() << " -> vkBindImageMemory\n";
        return (PFN_vkVoidFunction)vkBindImageMemory;
    }
    if (strcmp(pName, "vkBindImageMemory2") == 0) {
        ICD_LOG_INFO() << " -> vkBindImageMemory2\n";
        return (PFN_vkVoidFunction)vkBindImageMemory2;
    }
    if (strcmp(pName, "vkBindImageMemory2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkBindImageMemory2KHR\n";
        return (PFN_vkVoidFunction)vkBindImageMemory2KHR;
    }
    if (strcmp(pName, "vkCreateShaderModule") == 0) {
        ICD_LOG_INFO() << " -> vkCreateShaderModule\n";
        return (PFN_vkVoidFunction)vkCreateShaderModule;
    }
    if (strcmp(pName, "vkDestroyShaderModule") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyShaderModule\n";
        return (PFN_vkVoidFunction)vkDestroyShaderModule;
    }
    if (strcmp(pName, "vkCreateDescriptorSetLayout") == 0) {
        ICD_LOG_INFO() << " -> vkCreateDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    }
    if (strcmp(pName, "vkDestroyDescriptorSetLayout") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    }
    if (strcmp(pName, "vkCreateDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateDescriptorPool\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    }
    if (strcmp(pName, "vkDestroyDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDescriptorPool\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    }
    if (strcmp(pName, "vkResetDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetDescriptorPool\n";
        return (PFN_vkVoidFunction)vkResetDescriptorPool;
    }
    if (strcmp(pName, "vkAllocateDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    }
    if (strcmp(pName, "vkFreeDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkFreeDescriptorSets\n";
        return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    }
    if (strcmp(pName, "vkUpdateDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkUpdateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    }
    if (strcmp(pName, "vkCreateDescriptorUpdateTemplate") == 0 ||
        strcmp(pName, "vkCreateDescriptorUpdateTemplateKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCreateDescriptorUpdateTemplate\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorUpdateTemplate;
    }
    if (strcmp(pName, "vkDestroyDescriptorUpdateTemplate") == 0 ||
        strcmp(pName, "vkDestroyDescriptorUpdateTemplateKHR") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDescriptorUpdateTemplate\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorUpdateTemplate;
    }
    if (strcmp(pName, "vkUpdateDescriptorSetWithTemplate") == 0 ||
        strcmp(pName, "vkUpdateDescriptorSetWithTemplateKHR") == 0) {
        if (device_api_version() < VK_API_VERSION_1_1 &&
            !device_supports_extension(VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME)) {
            ICD_LOG_INFO() << " -> vkUpdateDescriptorSetWithTemplate (unsupported: descriptor_update_template not enabled)\n";
            return nullptr;
        }
        ICD_LOG_INFO() << " -> vkUpdateDescriptorSetWithTemplate\n";
        return (PFN_vkVoidFunction)vkUpdateDescriptorSetWithTemplate;
    }
    if (strcmp(pName, "vkCmdPushDescriptorSet") == 0 ||
        strcmp(pName, "vkCmdPushDescriptorSetKHR") == 0) {
        if (!device_supports_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            ICD_LOG_INFO() << " -> vkCmdPushDescriptorSet (unsupported: push_descriptor not enabled)\n";
            return nullptr;
        }
        ICD_LOG_INFO() << " -> vkCmdPushDescriptorSet\n";
        return (PFN_vkVoidFunction)vkCmdPushDescriptorSet;
    }
    if (strcmp(pName, "vkCmdPushDescriptorSetWithTemplate") == 0 ||
        strcmp(pName, "vkCmdPushDescriptorSetWithTemplateKHR") == 0) {
        if (!device_supports_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            ICD_LOG_INFO() << " -> vkCmdPushDescriptorSetWithTemplate (unsupported: push_descriptor not enabled)\n";
            return nullptr;
        }
        ICD_LOG_INFO() << " -> vkCmdPushDescriptorSetWithTemplate\n";
        return (PFN_vkVoidFunction)vkCmdPushDescriptorSetWithTemplate;
    }
    if (strcmp(pName, "vkCreatePipelineLayout") == 0) {
        ICD_LOG_INFO() << " -> vkCreatePipelineLayout\n";
        return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    }
    if (strcmp(pName, "vkDestroyPipelineLayout") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipelineLayout\n";
        return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    }
    if (strcmp(pName, "vkCreatePipelineCache") == 0) {
        ICD_LOG_INFO() << " -> vkCreatePipelineCache\n";
        return (PFN_vkVoidFunction)vkCreatePipelineCache;
    }
    if (strcmp(pName, "vkDestroyPipelineCache") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipelineCache\n";
        return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    }
    if (strcmp(pName, "vkGetPipelineCacheData") == 0) {
        ICD_LOG_INFO() << " -> vkGetPipelineCacheData\n";
        return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    }
    if (strcmp(pName, "vkMergePipelineCaches") == 0) {
        ICD_LOG_INFO() << " -> vkMergePipelineCaches\n";
        return (PFN_vkVoidFunction)vkMergePipelineCaches;
    }
    if (strcmp(pName, "vkCreateQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateQueryPool\n";
        return (PFN_vkVoidFunction)vkCreateQueryPool;
    }
    if (strcmp(pName, "vkDestroyQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyQueryPool\n";
        return (PFN_vkVoidFunction)vkDestroyQueryPool;
    }
    if (strcmp(pName, "vkResetQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetQueryPool\n";
        return (PFN_vkVoidFunction)vkResetQueryPool;
    }
    if (strcmp(pName, "vkGetQueryPoolResults") == 0) {
        ICD_LOG_INFO() << " -> vkGetQueryPoolResults\n";
        return (PFN_vkVoidFunction)vkGetQueryPoolResults;
    }
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSwapchainKHR\n";
        return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    }
    if (strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySwapchainKHR\n";
        return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    }
    if (strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetSwapchainImagesKHR\n";
        return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    }
    if (strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        ICD_LOG_INFO() << " -> vkAcquireNextImageKHR\n";
        return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    }
    if (strcmp(pName, "vkAcquireNextImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkAcquireNextImage2KHR\n";
        return (PFN_vkVoidFunction)vkAcquireNextImage2KHR;
    }
    if (strcmp(pName, "vkQueuePresentKHR") == 0) {
        ICD_LOG_INFO() << " -> vkQueuePresentKHR\n";
        return (PFN_vkVoidFunction)vkQueuePresentKHR;
    }
    if (strcmp(pName, "vkCreateRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCreateRenderPass\n";
        return (PFN_vkVoidFunction)vkCreateRenderPass;
    }
    if (strcmp(pName, "vkCreateRenderPass2") == 0 || strcmp(pName, "vkCreateRenderPass2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCreateRenderPass2\n";
        return (PFN_vkVoidFunction)vkCreateRenderPass2;
    }
    if (strcmp(pName, "vkDestroyRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyRenderPass\n";
        return (PFN_vkVoidFunction)vkDestroyRenderPass;
    }
    if (strcmp(pName, "vkGetRenderAreaGranularity") == 0) {
        ICD_LOG_INFO() << " -> vkGetRenderAreaGranularity\n";
        return (PFN_vkVoidFunction)vkGetRenderAreaGranularity;
    }
    if (strcmp(pName, "vkCreateFramebuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCreateFramebuffer\n";
        return (PFN_vkVoidFunction)vkCreateFramebuffer;
    }
    if (strcmp(pName, "vkDestroyFramebuffer") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyFramebuffer\n";
        return (PFN_vkVoidFunction)vkDestroyFramebuffer;
    }
    if (strcmp(pName, "vkCreateComputePipelines") == 0) {
        ICD_LOG_INFO() << " -> vkCreateComputePipelines\n";
        return (PFN_vkVoidFunction)vkCreateComputePipelines;
    }
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        ICD_LOG_INFO() << " -> vkCreateGraphicsPipelines\n";
        return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    }
    if (strcmp(pName, "vkGetPipelineExecutablePropertiesKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetPipelineExecutablePropertiesKHR\n";
        return (PFN_vkVoidFunction)vkGetPipelineExecutablePropertiesKHR;
    }
    if (strcmp(pName, "vkGetPipelineExecutableStatisticsKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetPipelineExecutableStatisticsKHR\n";
        return (PFN_vkVoidFunction)vkGetPipelineExecutableStatisticsKHR;
    }
    if (strcmp(pName, "vkGetPipelineExecutableInternalRepresentationsKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetPipelineExecutableInternalRepresentationsKHR\n";
        return (PFN_vkVoidFunction)vkGetPipelineExecutableInternalRepresentationsKHR;
    }
    if (strcmp(pName, "vkDestroyPipeline") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipeline\n";
        return (PFN_vkVoidFunction)vkDestroyPipeline;
    }
    if (strcmp(pName, "vkGetImageSubresourceLayout") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageSubresourceLayout\n";
        return (PFN_vkVoidFunction)vkGetImageSubresourceLayout;
    }
    if (strcmp(pName, "vkCreateCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateCommandPool\n";
        return (PFN_vkVoidFunction)vkCreateCommandPool;
    }
    if (strcmp(pName, "vkDestroyCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyCommandPool\n";
        return (PFN_vkVoidFunction)vkDestroyCommandPool;
    }
    if (strcmp(pName, "vkResetCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetCommandPool\n";
        return (PFN_vkVoidFunction)vkResetCommandPool;
    }
    if (strcmp(pName, "vkTrimCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkTrimCommandPool\n";
        return (PFN_vkVoidFunction)vkTrimCommandPool;
    }
    if (strcmp(pName, "vkTrimCommandPoolKHR") == 0) {
        ICD_LOG_INFO() << " -> vkTrimCommandPoolKHR\n";
        return (PFN_vkVoidFunction)vkTrimCommandPoolKHR;
    }
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateCommandBuffers\n";
        return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    }
    if (strcmp(pName, "vkFreeCommandBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkFreeCommandBuffers\n";
        return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    }
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkBeginCommandBuffer\n";
        return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    }
    if (strcmp(pName, "vkEndCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkEndCommandBuffer\n";
        return (PFN_vkVoidFunction)vkEndCommandBuffer;
    }
    if (strcmp(pName, "vkResetCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkResetCommandBuffer\n";
        return (PFN_vkVoidFunction)vkResetCommandBuffer;
    }
    if (strcmp(pName, "vkCmdSetBlendConstants") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetBlendConstants\n";
        return (PFN_vkVoidFunction)vkCmdSetBlendConstants;
    }
    if (strcmp(pName, "vkCmdSetLineWidth") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetLineWidth\n";
        return (PFN_vkVoidFunction)vkCmdSetLineWidth;
    }
    if (strcmp(pName, "vkCmdSetDepthBias") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBias\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBias;
    }
    if (strcmp(pName, "vkCmdSetDepthBounds") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBounds\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBounds;
    }
    if (strcmp(pName, "vkCmdSetStencilCompareMask") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilCompareMask\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilCompareMask;
    }
    if (strcmp(pName, "vkCmdSetStencilWriteMask") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilWriteMask\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilWriteMask;
    }
    if (strcmp(pName, "vkCmdSetStencilReference") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilReference\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilReference;
    }
    if (strcmp(pName, "vkCmdSetDeviceMask") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDeviceMask\n";
        return (PFN_vkVoidFunction)vkCmdSetDeviceMask;
    }
    if (strcmp(pName, "vkCmdSetDeviceMaskKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDeviceMaskKHR\n";
        return (PFN_vkVoidFunction)vkCmdSetDeviceMaskKHR;
    }
    if (strcmp(pName, "vkCmdCopyBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    }
    if (strcmp(pName, "vkCmdCopyBuffer2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBuffer2\n";
        return (PFN_vkVoidFunction)vkCmdCopyBuffer2;
    }
    if (strcmp(pName, "vkCmdCopyBuffer2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBuffer2KHR\n";
        return (PFN_vkVoidFunction)vkCmdCopyBuffer2KHR;
    }
    if (strcmp(pName, "vkCmdCopyImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyImage;
    }
    if (strcmp(pName, "vkCmdCopyImage2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImage2\n";
        return (PFN_vkVoidFunction)vkCmdCopyImage2;
    }
    if (strcmp(pName, "vkCmdCopyImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImage2KHR\n";
        return (PFN_vkVoidFunction)vkCmdCopyImage2KHR;
    }
    if (strcmp(pName, "vkCmdBlitImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBlitImage\n";
        return (PFN_vkVoidFunction)vkCmdBlitImage;
    }
    if (strcmp(pName, "vkCmdBlitImage2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBlitImage2\n";
        return (PFN_vkVoidFunction)vkCmdBlitImage2;
    }
    if (strcmp(pName, "vkCmdBlitImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBlitImage2KHR\n";
        return (PFN_vkVoidFunction)vkCmdBlitImage2KHR;
    }
    if (strcmp(pName, "vkCmdCopyBufferToImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBufferToImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    }
    if (strcmp(pName, "vkCmdCopyBufferToImage2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBufferToImage2\n";
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage2;
    }
    if (strcmp(pName, "vkCmdCopyBufferToImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBufferToImage2KHR\n";
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage2KHR;
    }
    if (strcmp(pName, "vkCmdCopyImageToBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImageToBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    }
    if (strcmp(pName, "vkCmdResolveImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResolveImage\n";
        return (PFN_vkVoidFunction)vkCmdResolveImage;
    }
    if (strcmp(pName, "vkCmdCopyImageToBuffer2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImageToBuffer2\n";
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer2;
    }
    if (strcmp(pName, "vkCmdCopyImageToBuffer2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImageToBuffer2KHR\n";
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer2KHR;
    }
    if (strcmp(pName, "vkCmdResolveImage2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResolveImage2\n";
        return (PFN_vkVoidFunction)vkCmdResolveImage2;
    }
    if (strcmp(pName, "vkCmdResolveImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResolveImage2KHR\n";
        return (PFN_vkVoidFunction)vkCmdResolveImage2KHR;
    }
    if (strcmp(pName, "vkCmdFillBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdFillBuffer\n";
        return (PFN_vkVoidFunction)vkCmdFillBuffer;
    }
    if (strcmp(pName, "vkCmdUpdateBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdUpdateBuffer\n";
        return (PFN_vkVoidFunction)vkCmdUpdateBuffer;
    }
    if (strcmp(pName, "vkCmdClearColorImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdClearColorImage\n";
        return (PFN_vkVoidFunction)vkCmdClearColorImage;
    }
    if (strcmp(pName, "vkCmdClearDepthStencilImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdClearDepthStencilImage\n";
        return (PFN_vkVoidFunction)vkCmdClearDepthStencilImage;
    }
    if (strcmp(pName, "vkCmdClearAttachments") == 0) {
        ICD_LOG_INFO() << " -> vkCmdClearAttachments\n";
        return (PFN_vkVoidFunction)vkCmdClearAttachments;
    }
    if (strcmp(pName, "vkCmdBeginRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginRenderPass\n";
        return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    }
    if (strcmp(pName, "vkCmdBeginRenderPass2") == 0 || strcmp(pName, "vkCmdBeginRenderPass2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginRenderPass2\n";
        return (PFN_vkVoidFunction)vkCmdBeginRenderPass2;
    }
    if (strcmp(pName, "vkCmdBeginRendering") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginRendering\n";
        return (PFN_vkVoidFunction)vkCmdBeginRendering;
    }
    if (strcmp(pName, "vkCmdBeginRenderingKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginRenderingKHR\n";
        return (PFN_vkVoidFunction)vkCmdBeginRenderingKHR;
    }
    if (strcmp(pName, "vkCmdEndRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndRenderPass\n";
        return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    }
    if (strcmp(pName, "vkCmdEndRenderPass2") == 0 || strcmp(pName, "vkCmdEndRenderPass2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndRenderPass2\n";
        return (PFN_vkVoidFunction)vkCmdEndRenderPass2;
    }
    if (strcmp(pName, "vkCmdEndRendering") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndRendering\n";
        return (PFN_vkVoidFunction)vkCmdEndRendering;
    }
    if (strcmp(pName, "vkCmdEndRenderingKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndRenderingKHR\n";
        return (PFN_vkVoidFunction)vkCmdEndRenderingKHR;
    }
    if (strcmp(pName, "vkCmdBindPipeline") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindPipeline\n";
        return (PFN_vkVoidFunction)vkCmdBindPipeline;
    }
    if (strcmp(pName, "vkCmdBindVertexBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindVertexBuffers\n";
        return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    }
    if (strcmp(pName, "vkCmdBindIndexBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindIndexBuffer\n";
        return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    }
    if (strcmp(pName, "vkCmdBindVertexBuffers2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindVertexBuffers2\n";
        return (PFN_vkVoidFunction)vkCmdBindVertexBuffers2;
    }
    if (strcmp(pName, "vkCmdBindVertexBuffers2EXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindVertexBuffers2EXT\n";
        return (PFN_vkVoidFunction)vkCmdBindVertexBuffers2EXT;
    }
    if (strcmp(pName, "vkCmdNextSubpass") == 0) {
        ICD_LOG_INFO() << " -> vkCmdNextSubpass\n";
        return (PFN_vkVoidFunction)vkCmdNextSubpass;
    }
    if (strcmp(pName, "vkCmdNextSubpass2") == 0 || strcmp(pName, "vkCmdNextSubpass2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdNextSubpass2\n";
        return (PFN_vkVoidFunction)vkCmdNextSubpass2;
    }
    if (strcmp(pName, "vkCmdSetViewport") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetViewport\n";
        return (PFN_vkVoidFunction)vkCmdSetViewport;
    }
    if (strcmp(pName, "vkCmdSetViewportWithCount") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetViewportWithCount\n";
        return (PFN_vkVoidFunction)vkCmdSetViewportWithCount;
    }
    if (strcmp(pName, "vkCmdSetViewportWithCountEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetViewportWithCountEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetViewportWithCountEXT;
    }
    if (strcmp(pName, "vkCmdSetScissor") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetScissor\n";
        return (PFN_vkVoidFunction)vkCmdSetScissor;
    }
    if (strcmp(pName, "vkCmdSetScissorWithCount") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetScissorWithCount\n";
        return (PFN_vkVoidFunction)vkCmdSetScissorWithCount;
    }
    if (strcmp(pName, "vkCmdSetScissorWithCountEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetScissorWithCountEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetScissorWithCountEXT;
    }
    if (strcmp(pName, "vkCmdSetCullMode") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetCullMode\n";
        return (PFN_vkVoidFunction)vkCmdSetCullMode;
    }
    if (strcmp(pName, "vkCmdSetCullModeEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetCullModeEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetCullModeEXT;
    }
    if (strcmp(pName, "vkCmdSetFrontFace") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetFrontFace\n";
        return (PFN_vkVoidFunction)vkCmdSetFrontFace;
    }
    if (strcmp(pName, "vkCmdSetFrontFaceEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetFrontFaceEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetFrontFaceEXT;
    }
    if (strcmp(pName, "vkCmdSetPrimitiveTopology") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetPrimitiveTopology\n";
        return (PFN_vkVoidFunction)vkCmdSetPrimitiveTopology;
    }
    if (strcmp(pName, "vkCmdSetPrimitiveTopologyEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetPrimitiveTopologyEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetPrimitiveTopologyEXT;
    }
    if (strcmp(pName, "vkCmdSetDepthTestEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthTestEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthTestEnable;
    }
    if (strcmp(pName, "vkCmdSetDepthTestEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthTestEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthTestEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetDepthWriteEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthWriteEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthWriteEnable;
    }
    if (strcmp(pName, "vkCmdSetDepthWriteEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthWriteEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthWriteEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetDepthCompareOp") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthCompareOp\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthCompareOp;
    }
    if (strcmp(pName, "vkCmdSetDepthCompareOpEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthCompareOpEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthCompareOpEXT;
    }
    if (strcmp(pName, "vkCmdSetDepthBoundsTestEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBoundsTestEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBoundsTestEnable;
    }
    if (strcmp(pName, "vkCmdSetDepthBoundsTestEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBoundsTestEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBoundsTestEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetStencilTestEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilTestEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilTestEnable;
    }
    if (strcmp(pName, "vkCmdSetStencilTestEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilTestEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilTestEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetStencilOp") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilOp\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilOp;
    }
    if (strcmp(pName, "vkCmdSetStencilOpEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetStencilOpEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetStencilOpEXT;
    }
    if (strcmp(pName, "vkCmdSetRasterizerDiscardEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetRasterizerDiscardEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetRasterizerDiscardEnable;
    }
    if (strcmp(pName, "vkCmdSetRasterizerDiscardEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetRasterizerDiscardEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetRasterizerDiscardEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetDepthBiasEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBiasEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBiasEnable;
    }
    if (strcmp(pName, "vkCmdSetDepthBiasEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetDepthBiasEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetDepthBiasEnableEXT;
    }
    if (strcmp(pName, "vkCmdSetPrimitiveRestartEnable") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetPrimitiveRestartEnable\n";
        return (PFN_vkVoidFunction)vkCmdSetPrimitiveRestartEnable;
    }
    if (strcmp(pName, "vkCmdSetPrimitiveRestartEnableEXT") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetPrimitiveRestartEnableEXT\n";
        return (PFN_vkVoidFunction)vkCmdSetPrimitiveRestartEnableEXT;
    }
    if (strcmp(pName, "vkCmdExecuteCommands") == 0) {
        ICD_LOG_INFO() << " -> vkCmdExecuteCommands\n";
        return (PFN_vkVoidFunction)vkCmdExecuteCommands;
    }
    if (strcmp(pName, "vkCmdDraw") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDraw\n";
        return (PFN_vkVoidFunction)vkCmdDraw;
    }
    if (strcmp(pName, "vkCmdDrawIndexed") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndexed\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    }
    if (strcmp(pName, "vkCmdDrawIndirect") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndirect\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndirect;
    }
    if (strcmp(pName, "vkCmdDrawIndirectCount") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndirectCount\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndirectCount;
    }
    if (strcmp(pName, "vkCmdDrawIndirectCountKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndirectCountKHR\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndirectCountKHR;
    }
    if (strcmp(pName, "vkCmdDrawIndexedIndirect") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndexedIndirect\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndexedIndirect;
    }
    if (strcmp(pName, "vkCmdDrawIndexedIndirectCount") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndexedIndirectCount\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndexedIndirectCount;
    }
    if (strcmp(pName, "vkCmdDrawIndexedIndirectCountKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDrawIndexedIndirectCountKHR\n";
        return (PFN_vkVoidFunction)vkCmdDrawIndexedIndirectCountKHR;
    }
    if (strcmp(pName, "vkCmdBindDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindDescriptorSets\n";
        return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    }
    if (strcmp(pName, "vkCmdDispatch") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatch\n";
        return (PFN_vkVoidFunction)vkCmdDispatch;
    }
    if (strcmp(pName, "vkCmdDispatchIndirect") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchIndirect\n";
        return (PFN_vkVoidFunction)vkCmdDispatchIndirect;
    }
    if (strcmp(pName, "vkCmdDispatchBase") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchBase\n";
        return (PFN_vkVoidFunction)vkCmdDispatchBase;
    }
    if (strcmp(pName, "vkCmdDispatchBaseKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchBaseKHR\n";
        return (PFN_vkVoidFunction)vkCmdDispatchBaseKHR;
    }
    if (strcmp(pName, "vkCmdPushConstants") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPushConstants\n";
        return (PFN_vkVoidFunction)vkCmdPushConstants;
    }
    if (strcmp(pName, "vkCmdPipelineBarrier") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPipelineBarrier\n";
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    }
    if (strcmp(pName, "vkCmdPipelineBarrier2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPipelineBarrier2\n";
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier2;
    }
    if (strcmp(pName, "vkCmdPipelineBarrier2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPipelineBarrier2KHR\n";
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier2KHR;
    }
    if (strcmp(pName, "vkCmdResetQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetQueryPool\n";
        return (PFN_vkVoidFunction)vkCmdResetQueryPool;
    }
    if (strcmp(pName, "vkCmdBeginQuery") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginQuery\n";
        return (PFN_vkVoidFunction)vkCmdBeginQuery;
    }
    if (strcmp(pName, "vkCmdEndQuery") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndQuery\n";
        return (PFN_vkVoidFunction)vkCmdEndQuery;
    }
    if (strcmp(pName, "vkCmdWriteTimestamp") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWriteTimestamp\n";
        return (PFN_vkVoidFunction)vkCmdWriteTimestamp;
    }
    if (strcmp(pName, "vkCmdWriteTimestamp2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWriteTimestamp2\n";
        return (PFN_vkVoidFunction)vkCmdWriteTimestamp2;
    }
    if (strcmp(pName, "vkCmdWriteTimestamp2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWriteTimestamp2KHR\n";
        return (PFN_vkVoidFunction)vkCmdWriteTimestamp2KHR;
    }
    if (strcmp(pName, "vkCmdCopyQueryPoolResults") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyQueryPoolResults\n";
        return (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults;
    }
    if (strcmp(pName, "vkCmdSetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetEvent\n";
        return (PFN_vkVoidFunction)vkCmdSetEvent;
    }
    if (strcmp(pName, "vkCmdSetEvent2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetEvent2\n";
        return (PFN_vkVoidFunction)vkCmdSetEvent2;
    }
    if (strcmp(pName, "vkCmdSetEvent2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetEvent2KHR\n";
        return (PFN_vkVoidFunction)vkCmdSetEvent2KHR;
    }
    if (strcmp(pName, "vkCmdResetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetEvent\n";
        return (PFN_vkVoidFunction)vkCmdResetEvent;
    }
    if (strcmp(pName, "vkCmdResetEvent2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetEvent2\n";
        return (PFN_vkVoidFunction)vkCmdResetEvent2;
    }
    if (strcmp(pName, "vkCmdResetEvent2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetEvent2KHR\n";
        return (PFN_vkVoidFunction)vkCmdResetEvent2KHR;
    }
    if (strcmp(pName, "vkCmdWaitEvents") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWaitEvents\n";
        return (PFN_vkVoidFunction)vkCmdWaitEvents;
    }
    if (strcmp(pName, "vkCmdWaitEvents2") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWaitEvents2\n";
        return (PFN_vkVoidFunction)vkCmdWaitEvents2;
    }
    if (strcmp(pName, "vkCmdWaitEvents2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWaitEvents2KHR\n";
        return (PFN_vkVoidFunction)vkCmdWaitEvents2KHR;
    }
    if (strcmp(pName, "vkCreateEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCreateEvent\n";
        return (PFN_vkVoidFunction)vkCreateEvent;
    }
    if (strcmp(pName, "vkDestroyEvent") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyEvent\n";
        return (PFN_vkVoidFunction)vkDestroyEvent;
    }
    if (strcmp(pName, "vkGetEventStatus") == 0) {
        ICD_LOG_INFO() << " -> vkGetEventStatus\n";
        return (PFN_vkVoidFunction)vkGetEventStatus;
    }
    if (strcmp(pName, "vkSetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkSetEvent\n";
        return (PFN_vkVoidFunction)vkSetEvent;
    }
    if (strcmp(pName, "vkResetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkResetEvent\n";
        return (PFN_vkVoidFunction)vkResetEvent;
    }
    if (strcmp(pName, "vkCreateFence") == 0) {
        ICD_LOG_INFO() << " -> vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        ICD_LOG_INFO() << " -> vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        ICD_LOG_INFO() << " -> vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        ICD_LOG_INFO() << " -> vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        ICD_LOG_INFO() << " -> vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        ICD_LOG_INFO() << " -> vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueBindSparse") == 0) {
        ICD_LOG_INFO() << " -> vkQueueBindSparse\n";
        return (PFN_vkVoidFunction)vkQueueBindSparse;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        ICD_LOG_INFO() << " -> vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueSubmit2") == 0) {
        ICD_LOG_INFO() << " -> vkQueueSubmit2\n";
        return (PFN_vkVoidFunction)vkQueueSubmit2;
    }
    if (strcmp(pName, "vkQueueSubmit2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkQueueSubmit2KHR\n";
        return (PFN_vkVoidFunction)vkQueueSubmit2KHR;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    }
    if (strcmp(pName, "vkGetDeviceGroupPeerMemoryFeatures") == 0 ||
        strcmp(pName, "vkGetDeviceGroupPeerMemoryFeaturesKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceGroupPeerMemoryFeatures\n";
        return (PFN_vkVoidFunction)vkGetDeviceGroupPeerMemoryFeatures;
    }

    ICD_LOG_INFO() << " -> NOT IMPLEMENTED, returning nullptr\n";
    return nullptr;
}

} // extern "C"
