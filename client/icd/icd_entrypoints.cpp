#include "icd_entrypoints.h"
#include "icd_instance.h"
#include "icd_device.h"
#include "network/network_client.h"
#include "state/handle_allocator.h"
#include "state/instance_state.h"
#include "state/device_state.h"
#include "state/resource_state.h"
#include "state/command_buffer_state.h"
#include "vn_protocol_driver.h"
#include "vn_ring.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>

using namespace venus_plus;

// For Phase 1-2, we'll use a simple global connection
static NetworkClient g_client;
static vn_ring g_ring = {};
static bool g_connected = false;

// Constructor - runs when the shared library is loaded
__attribute__((constructor))
static void icd_init() {
    std::cout << "\n===========================================\n";
    std::cout << "VENUS PLUS ICD LOADED!\n";
    std::cout << "===========================================\n\n";
}

static bool ensure_connected() {
    if (!g_connected) {
        // TODO: Get host/port from env variable
        if (!g_client.connect("127.0.0.1", 5556)) {
            return false;
        }
        g_ring.client = &g_client;
        g_connected = true;
    }
    return true;
}

static bool ensure_command_buffer_tracked(VkCommandBuffer commandBuffer, const char* func_name) {
    if (!g_command_buffer_state.has_command_buffer(commandBuffer)) {
        std::cerr << "[Client ICD] " << func_name << " called with unknown command buffer\n";
        return false;
    }
    return true;
}

static bool ensure_command_buffer_recording(VkCommandBuffer commandBuffer, const char* func_name) {
    if (!ensure_command_buffer_tracked(commandBuffer, func_name)) {
        return false;
    }
    CommandBufferLifecycleState state = g_command_buffer_state.get_buffer_state(commandBuffer);
    if (state != CommandBufferLifecycleState::RECORDING) {
        std::cerr << "[Client ICD] " << func_name << " requires RECORDING state (current="
                  << static_cast<int>(state) << ")\n";
        return false;
    }
    return true;
}

static VkCommandBuffer get_remote_command_buffer_handle(VkCommandBuffer commandBuffer) {
    VkCommandBuffer remote = g_command_buffer_state.get_remote_command_buffer(commandBuffer);
    if (remote != VK_NULL_HANDLE) {
        return remote;
    }
    IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(commandBuffer);
    return icd_cb ? icd_cb->remote_handle : VK_NULL_HANDLE;
}

extern "C" {

// Forward declarations for device-level functions (needed by vkGetDeviceProcAddr)
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

// ICD interface version negotiation
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    std::cout << "[Client ICD] vk_icdNegotiateLoaderICDInterfaceVersion called\n";
    std::cout << "[Client ICD] Loader requested version: " << *pSupportedVersion << "\n";

    // Use ICD interface version 7 (latest version)
    // Version 7 adds support for additional loader features
    if (*pSupportedVersion > 7) {
        *pSupportedVersion = 7;
    }

    std::cout << "[Client ICD] Negotiated version: " << *pSupportedVersion << "\n";
    return VK_SUCCESS;
}

// ICD GetInstanceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    std::cout << "[Client ICD] vk_icdGetInstanceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        std::cout << " -> returning nullptr\n";
        return nullptr;
    }

    // Return our implementations
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        std::cout << " -> returning vkEnumerateInstanceVersion\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        std::cout << " -> returning vkEnumerateInstanceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkCreateInstance") == 0) {
        std::cout << " -> returning vkCreateInstance\n";
        return (PFN_vkVoidFunction)vkCreateInstance;
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        std::cout << " -> returning vkGetInstanceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        std::cout << " -> returning vkDestroyInstance\n";
        return (PFN_vkVoidFunction)vkDestroyInstance;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        std::cout << " -> returning vkEnumeratePhysicalDevices\n";
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceFeatures\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceQueueFamilyProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceMemoryProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        std::cout << " -> returning vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (strcmp(pName, "vkCreateDevice") == 0) {
        std::cout << " -> returning vkCreateDevice\n";
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        std::cout << " -> returning vkEnumerateDeviceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties") == 0) {
        std::cout << " -> returning vkGetPhysicalDeviceSparseImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties;
    }

    std::cout << " -> NOT FOUND, returning nullptr\n";
    return nullptr;
}

// Standard vkGetInstanceProcAddr (required by spec)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

// ICD GetPhysicalDeviceProcAddr (required for ICD interface version 3+)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    std::cout << "[Client ICD] vk_icdGetPhysicalDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        std::cout << " -> returning nullptr\n";
        return nullptr;
    }

    // For Phase 2, we don't implement any physical device functions yet
    // Later phases will add these
    std::cout << " -> NOT IMPLEMENTED, returning nullptr\n";
    return nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    std::cout << "[Client ICD] vkEnumerateInstanceVersion called\n";

    // Return our supported Vulkan API version (1.3)
    // This is a static value, no server communication needed
    *pApiVersion = VK_API_VERSION_1_3;

    std::cout << "[Client ICD] Returning version: 1.3.0\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    std::cout << "[Client ICD] vkEnumerateInstanceExtensionProperties called\n";

    // We don't support layers
    if (pLayerName != nullptr) {
        std::cout << "[Client ICD] Layer requested: " << pLayerName << " -> VK_ERROR_LAYER_NOT_PRESENT\n";
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkEnumerateInstanceExtensionProperties(
        &g_ring, pLayerName, pPropertyCount, pProperties);

    if (result == VK_SUCCESS && pPropertyCount) {
        std::cout << "[Client ICD] Returning " << *pPropertyCount << " extensions\n";
    }

    return result;
}

// vkCreateInstance - Phase 2
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    std::cout << "[Client ICD] vkCreateInstance called\n";

    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Failed to connect to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 1. Allocate ICD instance structure (required for version 5 dispatch table)
    IcdInstance* icd_instance = new IcdInstance();
    if (!icd_instance) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // Initialize loader dispatch - will be filled by loader after we return
    icd_instance->loader_data = nullptr;

    icd_instance->remote_handle = VK_NULL_HANDLE;

    VkResult wire_result = vn_call_vkCreateInstance(&g_ring, pCreateInfo, pAllocator, &icd_instance->remote_handle);
    if (wire_result != VK_SUCCESS) {
        delete icd_instance;
        return wire_result;
    }

    // Return the ICD instance as the VkInstance handle. The loader will populate
    // icd_instance->loader_data after we return.
    *pInstance = icd_instance_to_handle(icd_instance);

    // Track the mapping between the loader-visible handle and the remote handle.
    g_instance_state.add_instance(*pInstance, icd_instance->remote_handle);

    std::cout << "[Client ICD] Instance created successfully\n";
    std::cout << "[Client ICD] Loader handle: " << *pInstance
              << ", remote handle: " << icd_instance->remote_handle << "\n";
    return VK_SUCCESS;
}

// vkDestroyInstance - Phase 2
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyInstance called for instance: " << instance << "\n";

    if (instance == VK_NULL_HANDLE) {
        return;
    }

    // Get ICD instance structure
    IcdInstance* icd_instance = icd_instance_from_handle(instance);
    VkInstance loader_handle = icd_instance_to_handle(icd_instance);

    if (g_connected) {
        vn_async_vkDestroyInstance(&g_ring, icd_instance->remote_handle, pAllocator);
    }

    if (g_instance_state.has_instance(loader_handle)) {
        g_instance_state.remove_instance(loader_handle);
    } else {
        std::cerr << "[Client ICD] Warning: Instance not tracked during destroy\n";
    }

    // Free the ICD instance structure
    delete icd_instance;

    std::cout << "[Client ICD] Instance destroyed\n";
}

// vkEnumeratePhysicalDevices - Phase 2
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {

    std::cout << "[Client ICD] vkEnumeratePhysicalDevices called\n";

    if (!pPhysicalDeviceCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdInstance* icd_instance = icd_instance_from_handle(instance);
    InstanceState* state = g_instance_state.get_instance(instance);
    if (!icd_instance || !state) {
        std::cerr << "[Client ICD] Invalid instance state\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance remote_instance = icd_instance->remote_handle;
    uint32_t requested_count = (pPhysicalDevices && *pPhysicalDeviceCount) ? *pPhysicalDeviceCount : 0;
    std::vector<VkPhysicalDevice> remote_devices;
    if (pPhysicalDevices && requested_count > 0) {
        remote_devices.resize(requested_count);
    }

    VkResult wire_result = vn_call_vkEnumeratePhysicalDevices(
        &g_ring,
        remote_instance,
        pPhysicalDeviceCount,
        pPhysicalDevices && requested_count > 0 ? remote_devices.data() : nullptr);

    if (wire_result != VK_SUCCESS) {
        return wire_result;
    }

    std::cout << "[Client ICD] Server reported " << *pPhysicalDeviceCount << " device(s)\n";

    if (!pPhysicalDevices) {
        return VK_SUCCESS;
    }

    const uint32_t returned = std::min<uint32_t>(remote_devices.size(), *pPhysicalDeviceCount);
    remote_devices.resize(returned);

    std::vector<PhysicalDeviceEntry> new_entries;
    new_entries.reserve(remote_devices.size());
    std::vector<VkPhysicalDevice> local_devices;
    local_devices.reserve(remote_devices.size());

    for (VkPhysicalDevice remote : remote_devices) {
        auto existing = std::find_if(
            state->physical_devices.begin(),
            state->physical_devices.end(),
            [remote](const PhysicalDeviceEntry& entry) {
                return entry.remote_handle == remote;
            });

        VkPhysicalDevice local = VK_NULL_HANDLE;
        if (existing != state->physical_devices.end()) {
            local = existing->local_handle;
        } else {
            local = g_handle_allocator.allocate<VkPhysicalDevice>();
        }

        new_entries.emplace_back(local, remote);
        local_devices.push_back(local);
    }

    state->physical_devices = std::move(new_entries);

    for (uint32_t i = 0; i < local_devices.size(); ++i) {
        pPhysicalDevices[i] = local_devices[i];
        std::cout << "[Client ICD] Physical device " << i << " local=" << local_devices[i]
                  << " remote=" << remote_devices[i] << "\n";
    }

    return VK_SUCCESS;
}

// vkGetPhysicalDeviceFeatures - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceFeatures called\n";

    if (!pFeatures) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFeatures(&g_ring, remote_device, pFeatures);
    std::cout << "[Client ICD] Returned features from server\n";
}

// vkGetPhysicalDeviceFormatProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceFormatProperties called\n";

    if (!pFormatProperties) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pFormatProperties, 0, sizeof(VkFormatProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFormatProperties(&g_ring, remote_device, format, pFormatProperties);
}

// vkGetPhysicalDeviceImageFormatProperties - Phase 2 stub
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties called\n";

    if (!pImageFormatProperties) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // For Phase 2: Return unsupported for all formats
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

// vkGetPhysicalDeviceProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceProperties called\n";

    if (!pProperties) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceProperties(&g_ring, remote_device, pProperties);
    std::cout << "[Client ICD] Returned device properties from server: " << pProperties->deviceName << "\n";
}

// vkGetPhysicalDeviceQueueFamilyProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceQueueFamilyProperties called\n";

    if (!pQueueFamilyPropertyCount) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceQueueFamilyProperties(&g_ring, remote_device, pQueueFamilyPropertyCount, pQueueFamilyProperties);

    if (pQueueFamilyProperties) {
        std::cout << "[Client ICD] Returned " << *pQueueFamilyPropertyCount << " queue families from server\n";
    } else {
        std::cout << "[Client ICD] Returning queue family count: " << *pQueueFamilyPropertyCount << "\n";
    }
}

// vkGetPhysicalDeviceMemoryProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceMemoryProperties called\n";

    if (!pMemoryProperties) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceMemoryProperties(&g_ring, remote_device, pMemoryProperties);
    std::cout << "[Client ICD] Returned memory properties from server: "
              << pMemoryProperties->memoryTypeCount << " types, "
              << pMemoryProperties->memoryHeapCount << " heaps\n";
}

// vkGetDeviceProcAddr - Phase 3
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    std::cout << "[Client ICD] vkGetDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (!pName) {
        std::cout << " -> nullptr\n";
        return nullptr;
    }

    // Critical: vkGetDeviceProcAddr must return itself
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        std::cout << " -> vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }

    // Phase 3: Device-level functions
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        std::cout << " -> vkGetDeviceQueue\n";
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        std::cout << " -> vkDestroyDevice\n";
        return (PFN_vkVoidFunction)vkDestroyDevice;
    }
    if (strcmp(pName, "vkAllocateMemory") == 0) {
        std::cout << " -> vkAllocateMemory\n";
        return (PFN_vkVoidFunction)vkAllocateMemory;
    }
    if (strcmp(pName, "vkFreeMemory") == 0) {
        std::cout << " -> vkFreeMemory\n";
        return (PFN_vkVoidFunction)vkFreeMemory;
    }
    if (strcmp(pName, "vkCreateBuffer") == 0) {
        std::cout << " -> vkCreateBuffer\n";
        return (PFN_vkVoidFunction)vkCreateBuffer;
    }
    if (strcmp(pName, "vkDestroyBuffer") == 0) {
        std::cout << " -> vkDestroyBuffer\n";
        return (PFN_vkVoidFunction)vkDestroyBuffer;
    }
    if (strcmp(pName, "vkGetBufferMemoryRequirements") == 0) {
        std::cout << " -> vkGetBufferMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    }
    if (strcmp(pName, "vkBindBufferMemory") == 0) {
        std::cout << " -> vkBindBufferMemory\n";
        return (PFN_vkVoidFunction)vkBindBufferMemory;
    }
    if (strcmp(pName, "vkCreateImage") == 0) {
        std::cout << " -> vkCreateImage\n";
        return (PFN_vkVoidFunction)vkCreateImage;
    }
    if (strcmp(pName, "vkDestroyImage") == 0) {
        std::cout << " -> vkDestroyImage\n";
        return (PFN_vkVoidFunction)vkDestroyImage;
    }
    if (strcmp(pName, "vkGetImageMemoryRequirements") == 0) {
        std::cout << " -> vkGetImageMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    }
    if (strcmp(pName, "vkBindImageMemory") == 0) {
        std::cout << " -> vkBindImageMemory\n";
        return (PFN_vkVoidFunction)vkBindImageMemory;
    }
    if (strcmp(pName, "vkGetImageSubresourceLayout") == 0) {
        std::cout << " -> vkGetImageSubresourceLayout\n";
        return (PFN_vkVoidFunction)vkGetImageSubresourceLayout;
    }
    if (strcmp(pName, "vkCreateCommandPool") == 0) {
        std::cout << " -> vkCreateCommandPool\n";
        return (PFN_vkVoidFunction)vkCreateCommandPool;
    }
    if (strcmp(pName, "vkDestroyCommandPool") == 0) {
        std::cout << " -> vkDestroyCommandPool\n";
        return (PFN_vkVoidFunction)vkDestroyCommandPool;
    }
    if (strcmp(pName, "vkResetCommandPool") == 0) {
        std::cout << " -> vkResetCommandPool\n";
        return (PFN_vkVoidFunction)vkResetCommandPool;
    }
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) {
        std::cout << " -> vkAllocateCommandBuffers\n";
        return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    }
    if (strcmp(pName, "vkFreeCommandBuffers") == 0) {
        std::cout << " -> vkFreeCommandBuffers\n";
        return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    }
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) {
        std::cout << " -> vkBeginCommandBuffer\n";
        return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    }
    if (strcmp(pName, "vkEndCommandBuffer") == 0) {
        std::cout << " -> vkEndCommandBuffer\n";
        return (PFN_vkVoidFunction)vkEndCommandBuffer;
    }
    if (strcmp(pName, "vkResetCommandBuffer") == 0) {
        std::cout << " -> vkResetCommandBuffer\n";
        return (PFN_vkVoidFunction)vkResetCommandBuffer;
    }
    if (strcmp(pName, "vkCmdCopyBuffer") == 0) {
        std::cout << " -> vkCmdCopyBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    }
    if (strcmp(pName, "vkCmdCopyImage") == 0) {
        std::cout << " -> vkCmdCopyImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyImage;
    }
    if (strcmp(pName, "vkCmdBlitImage") == 0) {
        std::cout << " -> vkCmdBlitImage\n";
        return (PFN_vkVoidFunction)vkCmdBlitImage;
    }
    if (strcmp(pName, "vkCmdCopyBufferToImage") == 0) {
        std::cout << " -> vkCmdCopyBufferToImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    }
    if (strcmp(pName, "vkCmdCopyImageToBuffer") == 0) {
        std::cout << " -> vkCmdCopyImageToBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    }
    if (strcmp(pName, "vkCmdFillBuffer") == 0) {
        std::cout << " -> vkCmdFillBuffer\n";
        return (PFN_vkVoidFunction)vkCmdFillBuffer;
    }
    if (strcmp(pName, "vkCmdUpdateBuffer") == 0) {
        std::cout << " -> vkCmdUpdateBuffer\n";
        return (PFN_vkVoidFunction)vkCmdUpdateBuffer;
    }
    if (strcmp(pName, "vkCmdClearColorImage") == 0) {
        std::cout << " -> vkCmdClearColorImage\n";
        return (PFN_vkVoidFunction)vkCmdClearColorImage;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        // Return nullptr for now - not implemented in Phase 3
        std::cout << " -> NOT IMPLEMENTED\n";
        return nullptr;
    }

    std::cout << " -> NOT IMPLEMENTED, returning nullptr\n";
    return nullptr;
}

// vkEnumerateDeviceExtensionProperties - Phase 2 stub
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    std::cout << "[Client ICD] vkEnumerateDeviceExtensionProperties called\n";

    // We don't support layers
    if (pLayerName != nullptr) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    // For Phase 2: Return 0 device extensions
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

// vkGetPhysicalDeviceSparseImageFormatProperties - Phase 2 stub
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t* pPropertyCount,
    VkSparseImageFormatProperties* pProperties) {

    std::cout << "[Client ICD] vkGetPhysicalDeviceSparseImageFormatProperties called\n";

    if (!pPropertyCount) {
        return;
    }

    // For Phase 2: Return 0 sparse properties (we don't support sparse)
    *pPropertyCount = 0;
}

// vkCreateDevice - Phase 3
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    std::cout << "[Client ICD] vkCreateDevice called\n";

    if (!pCreateInfo || !pDevice) {
        std::cerr << "[Client ICD] Invalid parameters\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_physical_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_physical_device = entry.remote_handle;
                break;
            }
        }
    }

    if (remote_physical_device == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Failed to find remote physical device\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Allocate ICD device structure (required for loader dispatch table)
    IcdDevice* icd_device = new IcdDevice();
    if (!icd_device) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // Initialize loader_data - will be filled by loader after we return
    icd_device->loader_data = nullptr;
    icd_device->physical_device = physicalDevice;
    icd_device->remote_handle = VK_NULL_HANDLE;

    // Call server to create device
    VkResult result = vn_call_vkCreateDevice(&g_ring, remote_physical_device, pCreateInfo, pAllocator, &icd_device->remote_handle);

    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateDevice failed: " << result << "\n";
        delete icd_device;
        return result;
    }

    // Return the ICD device as VkDevice handle
    *pDevice = icd_device_to_handle(icd_device);

    // Store device mapping
    g_device_state.add_device(*pDevice, icd_device->remote_handle, physicalDevice);

    std::cout << "[Client ICD] Device created successfully (local=" << *pDevice
              << ", remote=" << icd_device->remote_handle << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDevice - Phase 3
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyDevice called for device: " << device << "\n";

    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Get ICD device structure
    IcdDevice* icd_device = icd_device_from_handle(device);

    // Clean up any command pools/buffers owned by this device
    std::vector<VkCommandBuffer> buffers_to_free;
    g_command_buffer_state.remove_device(device, &buffers_to_free, nullptr);
    for (VkCommandBuffer buffer : buffers_to_free) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(buffer);
        delete icd_cb;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        // Still clean up local resources
        g_resource_state.remove_device_resources(device);
        g_device_state.remove_device(device);
        delete icd_device;
        return;
    }

    // Call server to destroy device
    vn_async_vkDestroyDevice(&g_ring, icd_device->remote_handle, pAllocator);

    // Drop resource tracking for this device
    g_resource_state.remove_device_resources(device);

    // Remove from state
    g_device_state.remove_device(device);

    // Free the ICD device structure
    delete icd_device;

    std::cout << "[Client ICD] Device destroyed\n";
}

// vkGetDeviceQueue - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue) {

    std::cout << "[Client ICD] vkGetDeviceQueue called (device=" << device
              << ", family=" << queueFamilyIndex << ", index=" << queueIndex << ")\n";

    if (!pQueue) {
        std::cerr << "[Client ICD] pQueue is NULL\n";
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        *pQueue = VK_NULL_HANDLE;
        return;
    }

    // Get ICD device structure
    IcdDevice* icd_device = icd_device_from_handle(device);

    // Allocate ICD queue structure (required for loader dispatch table)
    IcdQueue* icd_queue = new IcdQueue();
    if (!icd_queue) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }

    // Initialize queue structure
    icd_queue->loader_data = nullptr;  // Loader will fill this
    icd_queue->parent_device = device;
    icd_queue->family_index = queueFamilyIndex;
    icd_queue->queue_index = queueIndex;
    icd_queue->remote_handle = VK_NULL_HANDLE;

    // Call server to get queue
    vn_async_vkGetDeviceQueue(&g_ring, icd_device->remote_handle, queueFamilyIndex, queueIndex, &icd_queue->remote_handle);

    // Return the ICD queue as VkQueue handle
    *pQueue = icd_queue_to_handle(icd_queue);

    // Store queue mapping
    g_device_state.add_queue(device, *pQueue, icd_queue->remote_handle, queueFamilyIndex, queueIndex);

    std::cout << "[Client ICD] Queue retrieved (local=" << *pQueue
              << ", remote=" << icd_queue->remote_handle << ")\n";
}

// vkAllocateMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory) {

    std::cout << "[Client ICD] vkAllocateMemory called\n";

    if (!pAllocateInfo || !pMemory) {
        std::cerr << "[Client ICD] Invalid parameters for vkAllocateMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkAllocateMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkDeviceMemory remote_memory = VK_NULL_HANDLE;
    VkResult result = vn_call_vkAllocateMemory(&g_ring, remote_device, pAllocateInfo, pAllocator, &remote_memory);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkAllocateMemory failed: " << result << "\n";
        return result;
    }

    VkDeviceMemory local_memory = g_handle_allocator.allocate<VkDeviceMemory>();
    g_resource_state.add_memory(device, local_memory, remote_memory, *pAllocateInfo);
    *pMemory = local_memory;

    std::cout << "[Client ICD] Memory allocated (local=" << *pMemory
              << ", remote=" << remote_memory
              << ", size=" << pAllocateInfo->allocationSize << ")\n";
    return VK_SUCCESS;
}

// vkFreeMemory - Phase 4
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkFreeMemory called\n";

    if (memory == VK_NULL_HANDLE) {
        return;
    }

    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    if (remote_memory == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote memory handle missing in vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkFreeMemory(&g_ring, icd_device->remote_handle, remote_memory, pAllocator);
    g_resource_state.remove_memory(memory);
    std::cout << "[Client ICD] Memory freed (local=" << memory << ", remote=" << remote_memory << ")\n";
}

// vkCreateBuffer - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device,
    const VkBufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkBuffer* pBuffer) {

    std::cout << "[Client ICD] vkCreateBuffer called\n";

    if (!pCreateInfo || !pBuffer) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkBuffer remote_buffer = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateBuffer(&g_ring, remote_device, pCreateInfo, pAllocator, &remote_buffer);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateBuffer failed: " << result << "\n";
        return result;
    }

    VkBuffer local_buffer = g_handle_allocator.allocate<VkBuffer>();
    g_resource_state.add_buffer(device, local_buffer, remote_buffer, *pCreateInfo);
    *pBuffer = local_buffer;

    std::cout << "[Client ICD] Buffer created (local=" << *pBuffer
              << ", remote=" << remote_buffer
              << ", size=" << pCreateInfo->size << ")\n";
    return VK_SUCCESS;
}

// vkDestroyBuffer - Phase 4
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device,
    VkBuffer buffer,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyBuffer called\n";

    if (buffer == VK_NULL_HANDLE) {
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyBuffer\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyBuffer\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    if (remote_buffer == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote buffer handle missing\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyBuffer(&g_ring, icd_device->remote_handle, remote_buffer, pAllocator);
    g_resource_state.remove_buffer(buffer);
    std::cout << "[Client ICD] Buffer destroyed (local=" << buffer << ", remote=" << remote_buffer << ")\n";
}

// vkGetBufferMemoryRequirements - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device,
    VkBuffer buffer,
    VkMemoryRequirements* pMemoryRequirements) {

    std::cout << "[Client ICD] vkGetBufferMemoryRequirements called\n";

    if (!pMemoryRequirements) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkGetBufferMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Buffer not tracked in vkGetBufferMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetBufferMemoryRequirements(&g_ring, icd_device->remote_handle, remote_buffer, pMemoryRequirements);
    g_resource_state.cache_buffer_requirements(buffer, *pMemoryRequirements);

    std::cout << "[Client ICD] Buffer memory requirements: size=" << pMemoryRequirements->size
              << ", alignment=" << pMemoryRequirements->alignment << "\n";
}

static bool validate_memory_offset(const VkMemoryRequirements& requirements,
                                   VkDeviceSize memory_size,
                                   VkDeviceSize offset) {
    if (requirements.alignment != 0 && (offset % requirements.alignment) != 0) {
        return false;
    }
    if (memory_size != 0 && offset + requirements.size > memory_size) {
        return false;
    }
    return true;
}

// vkBindBufferMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {

    std::cout << "[Client ICD] vkBindBufferMemory called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_buffer(buffer) || !g_resource_state.has_memory(memory)) {
        std::cerr << "[Client ICD] Buffer or memory not tracked in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_resource_state.buffer_is_bound(buffer)) {
        std::cerr << "[Client ICD] Buffer already bound to memory\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMemoryRequirements cached_requirements;
    if (g_resource_state.get_cached_buffer_requirements(buffer, &cached_requirements)) {
        VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
        if (!validate_memory_offset(cached_requirements, memory_size, memoryOffset)) {
            std::cerr << "[Client ICD] Buffer bind validation failed (offset=" << memoryOffset << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_buffer == VK_NULL_HANDLE || remote_memory == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote handles missing in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindBufferMemory(&g_ring, icd_device->remote_handle, remote_buffer, remote_memory, memoryOffset);
    if (result == VK_SUCCESS) {
        g_resource_state.bind_buffer(buffer, memory, memoryOffset);
        std::cout << "[Client ICD] Buffer bound to memory (buffer=" << buffer
                  << ", memory=" << memory << ", offset=" << memoryOffset << ")\n";
    } else {
        std::cerr << "[Client ICD] Server rejected vkBindBufferMemory: " << result << "\n";
    }
    return result;
}

// vkCreateImage - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {

    std::cout << "[Client ICD] vkCreateImage called\n";

    if (!pCreateInfo || !pImage) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkImage remote_image = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateImage(&g_ring, remote_device, pCreateInfo, pAllocator, &remote_image);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateImage failed: " << result << "\n";
        return result;
    }

    VkImage local_image = g_handle_allocator.allocate<VkImage>();
    g_resource_state.add_image(device, local_image, remote_image, *pCreateInfo);
    *pImage = local_image;

    std::cout << "[Client ICD] Image created (local=" << *pImage
              << ", remote=" << remote_image
              << ", format=" << pCreateInfo->format
              << ", extent=" << pCreateInfo->extent.width << "x"
              << pCreateInfo->extent.height << ")\n";
    return VK_SUCCESS;
}

// vkDestroyImage - Phase 4
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyImage called\n";

    if (image == VK_NULL_HANDLE) {
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyImage\n";
        g_resource_state.remove_image(image);
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyImage\n";
        g_resource_state.remove_image(image);
        return;
    }

    if (remote_image == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote image handle missing\n";
        g_resource_state.remove_image(image);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyImage(&g_ring, icd_device->remote_handle, remote_image, pAllocator);
    g_resource_state.remove_image(image);
    std::cout << "[Client ICD] Image destroyed (local=" << image << ", remote=" << remote_image << ")\n";
}

// vkGetImageMemoryRequirements - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device,
    VkImage image,
    VkMemoryRequirements* pMemoryRequirements) {

    std::cout << "[Client ICD] vkGetImageMemoryRequirements called\n";

    if (!pMemoryRequirements) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkGetImageMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    if (remote_image == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Image not tracked in vkGetImageMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageMemoryRequirements(&g_ring, icd_device->remote_handle, remote_image, pMemoryRequirements);
    g_resource_state.cache_image_requirements(image, *pMemoryRequirements);

    std::cout << "[Client ICD] Image memory requirements: size=" << pMemoryRequirements->size
              << ", alignment=" << pMemoryRequirements->alignment << "\n";
}

// vkBindImageMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {

    std::cout << "[Client ICD] vkBindImageMemory called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_image(image) || !g_resource_state.has_memory(memory)) {
        std::cerr << "[Client ICD] Image or memory not tracked in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_resource_state.image_is_bound(image)) {
        std::cerr << "[Client ICD] Image already bound to memory\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMemoryRequirements cached_requirements = {};
    VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
    if (g_resource_state.get_cached_image_requirements(image, &cached_requirements)) {
        if (!validate_memory_offset(cached_requirements, memory_size, memoryOffset)) {
            std::cerr << "[Client ICD] Image bind validation failed (offset=" << memoryOffset << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_image == VK_NULL_HANDLE || remote_memory == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote handles missing in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindImageMemory(&g_ring, icd_device->remote_handle, remote_image, remote_memory, memoryOffset);
    if (result == VK_SUCCESS) {
        g_resource_state.bind_image(image, memory, memoryOffset);
        std::cout << "[Client ICD] Image bound to memory (image=" << image
                  << ", memory=" << memory << ", offset=" << memoryOffset << ")\n";
    } else {
        std::cerr << "[Client ICD] Server rejected vkBindImageMemory: " << result << "\n";
    }
    return result;
}

// vkGetImageSubresourceLayout - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice device,
    VkImage image,
    const VkImageSubresource* pSubresource,
    VkSubresourceLayout* pLayout) {

    std::cout << "[Client ICD] vkGetImageSubresourceLayout called\n";

    if (!pSubresource || !pLayout) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkGetImageSubresourceLayout\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    if (remote_image == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Image not tracked in vkGetImageSubresourceLayout\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageSubresourceLayout(&g_ring, icd_device->remote_handle, remote_image, pSubresource, pLayout);
}

// vkCreateCommandPool - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool) {

    std::cout << "[Client ICD] vkCreateCommandPool called\n";

    if (!pCreateInfo || !pCommandPool) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkCommandPool remote_pool = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateCommandPool(&g_ring, icd_device->remote_handle, pCreateInfo, pAllocator, &remote_pool);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateCommandPool failed: " << result << "\n";
        return result;
    }

    VkCommandPool local_pool = g_handle_allocator.allocate<VkCommandPool>();
    *pCommandPool = local_pool;
    g_command_buffer_state.add_pool(device, local_pool, remote_pool, *pCreateInfo);

    std::cout << "[Client ICD] Command pool created (local=" << local_pool
              << ", family=" << pCreateInfo->queueFamilyIndex << ")\n";
    return VK_SUCCESS;
}

// vkDestroyCommandPool - Phase 5
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyCommandPool called\n";

    if (commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    std::vector<VkCommandBuffer> buffers_to_free;
    g_command_buffer_state.remove_pool(commandPool, &buffers_to_free);

    for (VkCommandBuffer buffer : buffers_to_free) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(buffer);
        delete icd_cb;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyCommandPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyCommandPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command pool handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyCommandPool(&g_ring, icd_device->remote_handle, remote_pool, pAllocator);
    std::cout << "[Client ICD] Command pool destroyed (local=" << commandPool << ")\n";
}

// vkResetCommandPool - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandPoolResetFlags flags) {

    std::cout << "[Client ICD] vkResetCommandPool called\n";

    if (!g_command_buffer_state.has_pool(commandPool)) {
        std::cerr << "[Client ICD] Unknown command pool in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote pool missing in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetCommandPool(&g_ring, icd_device->remote_handle, remote_pool, flags);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.reset_pool(commandPool);
        std::cout << "[Client ICD] Command pool reset\n";
    } else {
        std::cerr << "[Client ICD] vkResetCommandPool failed: " << result << "\n";
    }
    return result;
}

// vkAllocateCommandBuffers - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {

    std::cout << "[Client ICD] vkAllocateCommandBuffers called\n";

    if (!pAllocateInfo || !pCommandBuffers || pAllocateInfo->commandBufferCount == 0) {
        std::cerr << "[Client ICD] Invalid parameters for vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool command_pool = pAllocateInfo->commandPool;
    if (!g_command_buffer_state.has_pool(command_pool)) {
        std::cerr << "[Client ICD] Command pool not tracked in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_command_buffer_state.get_pool_device(command_pool) != device) {
        std::cerr << "[Client ICD] Command pool not owned by device\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(command_pool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command pool missing in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    uint32_t count = pAllocateInfo->commandBufferCount;
    std::vector<VkCommandBuffer> remote_buffers(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo remote_info = *pAllocateInfo;
    remote_info.commandPool = remote_pool;
    VkResult result = vn_call_vkAllocateCommandBuffers(&g_ring, icd_device->remote_handle, &remote_info, remote_buffers.data());
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkAllocateCommandBuffers failed: " << result << "\n";
        return result;
    }

    uint32_t allocated = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (remote_buffers[i] == VK_NULL_HANDLE) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            break;
        }

        IcdCommandBuffer* icd_cb = new (std::nothrow) IcdCommandBuffer();
        if (!icd_cb) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            break;
        }

        icd_cb->loader_data = nullptr;
        icd_cb->remote_handle = remote_buffers[i];
        icd_cb->parent_device = device;
        icd_cb->parent_pool = command_pool;
        icd_cb->level = pAllocateInfo->level;

        VkCommandBuffer local_handle = icd_command_buffer_to_handle(icd_cb);
        pCommandBuffers[i] = local_handle;
        g_command_buffer_state.add_command_buffer(command_pool, local_handle, remote_buffers[i], pAllocateInfo->level);
        allocated++;
    }

    if (result != VK_SUCCESS) {
        for (uint32_t i = 0; i < allocated; ++i) {
            g_command_buffer_state.remove_command_buffer(pCommandBuffers[i]);
            IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(pCommandBuffers[i]);
            delete icd_cb;
            pCommandBuffers[i] = VK_NULL_HANDLE;
        }
        vn_async_vkFreeCommandBuffers(&g_ring, icd_device->remote_handle, remote_pool, count, remote_buffers.data());
        return result;
    }

    std::cout << "[Client ICD] Allocated " << count << " command buffer(s)\n";
    return VK_SUCCESS;
}

// vkFreeCommandBuffers - Phase 5
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers) {

    std::cout << "[Client ICD] vkFreeCommandBuffers called\n";

    if (commandBufferCount == 0 || !pCommandBuffers) {
        return;
    }

    if (!g_command_buffer_state.has_pool(commandPool)) {
        std::cerr << "[Client ICD] Unknown command pool in vkFreeCommandBuffers\n";
        return;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command pool missing in vkFreeCommandBuffers\n";
        return;
    }
    std::vector<VkCommandBuffer> remote_handles;
    std::vector<VkCommandBuffer> local_handles;
    remote_handles.reserve(commandBufferCount);
    local_handles.reserve(commandBufferCount);

    for (uint32_t i = 0; i < commandBufferCount; ++i) {
        VkCommandBuffer handle = pCommandBuffers[i];
        if (handle == VK_NULL_HANDLE) {
            continue;
        }
        if (!g_command_buffer_state.has_command_buffer(handle)) {
            std::cerr << "[Client ICD] vkFreeCommandBuffers skipping unknown buffer " << handle << "\n";
            continue;
        }
        if (g_command_buffer_state.get_buffer_pool(handle) != commandPool) {
            std::cerr << "[Client ICD] vkFreeCommandBuffers: buffer " << handle << " not from pool\n";
            continue;
        }
        VkCommandBuffer remote_cb = get_remote_command_buffer_handle(handle);
        if (remote_cb != VK_NULL_HANDLE) {
            remote_handles.push_back(remote_cb);
        }
        g_command_buffer_state.remove_command_buffer(handle);
        local_handles.push_back(handle);
    }

    for (VkCommandBuffer handle : local_handles) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(handle);
        delete icd_cb;
    }

    if (remote_handles.empty()) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkFreeCommandBuffers\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkFreeCommandBuffers\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkFreeCommandBuffers(&g_ring,
                                  icd_device->remote_handle,
                                  remote_pool,
                                  static_cast<uint32_t>(remote_handles.size()),
                                  remote_handles.data());
    std::cout << "[Client ICD] Freed " << remote_handles.size() << " command buffer(s)\n";
}

// vkBeginCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {

    std::cout << "[Client ICD] vkBeginCommandBuffer called\n";

    if (!pBeginInfo) {
        std::cerr << "[Client ICD] pBeginInfo is NULL in vkBeginCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_command_buffer_tracked(commandBuffer, "vkBeginCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    CommandBufferLifecycleState state = g_command_buffer_state.get_buffer_state(commandBuffer);
    if (state == CommandBufferLifecycleState::RECORDING) {
        std::cerr << "[Client ICD] Command buffer already recording\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (state == CommandBufferLifecycleState::EXECUTABLE &&
        !(pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)) {
        std::cerr << "[Client ICD] vkBeginCommandBuffer requires SIMULTANEOUS_USE when re-recording\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (state == CommandBufferLifecycleState::INVALID) {
        std::cerr << "[Client ICD] Command buffer is invalid\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkBeginCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkBeginCommandBuffer(&g_ring, remote_cb, pBeginInfo);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::RECORDING);
        g_command_buffer_state.set_usage_flags(commandBuffer, pBeginInfo->flags);
        std::cout << "[Client ICD] Command buffer recording begun\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        std::cerr << "[Client ICD] vkBeginCommandBuffer failed: " << result << "\n";
    }
    return result;
}

// vkEndCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    std::cout << "[Client ICD] vkEndCommandBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkEndCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkEndCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkEndCommandBuffer(&g_ring, remote_cb);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::EXECUTABLE);
        std::cout << "[Client ICD] Command buffer recording ended\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        std::cerr << "[Client ICD] vkEndCommandBuffer failed: " << result << "\n";
    }
    return result;
}

// vkResetCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {

    std::cout << "[Client ICD] vkResetCommandBuffer called\n";

    if (!ensure_command_buffer_tracked(commandBuffer, "vkResetCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool pool = g_command_buffer_state.get_buffer_pool(commandBuffer);
    if (pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Unable to determine parent pool in vkResetCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPoolCreateFlags pool_flags = g_command_buffer_state.get_pool_flags(pool);
    if (!(pool_flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)) {
        std::cerr << "[Client ICD] Command pool does not support individual reset\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkResetCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkResetCommandBuffer(&g_ring, remote_cb, flags);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INITIAL);
        g_command_buffer_state.set_usage_flags(commandBuffer, 0);
        std::cout << "[Client ICD] Command buffer reset\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        std::cerr << "[Client ICD] vkResetCommandBuffer failed: " << result << "\n";
    }
    return result;
}

static bool validate_buffer_regions(uint32_t count, const void* regions, const char* func_name) {
    if (count == 0 || !regions) {
        std::cerr << "[Client ICD] " << func_name << " requires valid regions\n";
        return false;
    }
    return true;
}

static bool ensure_remote_buffer(VkBuffer buffer, VkBuffer* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_buffer(buffer);
    if (*remote == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] " << func_name << " buffer not tracked\n";
        return false;
    }
    return true;
}

static bool ensure_remote_image(VkImage image, VkImage* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_image(image);
    if (*remote == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] " << func_name << " image not tracked\n";
        return false;
    }
    return true;
}

// vkCmdCopyBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const VkBufferCopy* pRegions) {

    std::cout << "[Client ICD] vkCmdCopyBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBuffer") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyBuffer")) {
        return;
    }

    VkBuffer remote_src = VK_NULL_HANDLE;
    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(srcBuffer, &remote_src, "vkCmdCopyBuffer") ||
        !ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdCopyBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdCopyBuffer\n";
        return;
    }
    vn_async_vkCmdCopyBuffer(&g_ring, remote_cb, remote_src, remote_dst, regionCount, pRegions);
    std::cout << "[Client ICD] vkCmdCopyBuffer recorded (" << regionCount << " regions)\n";
}

// vkCmdCopyImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions) {

    std::cout << "[Client ICD] vkCmdCopyImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyImage")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdCopyImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdCopyImage")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdCopyImage\n";
        return;
    }
    vn_async_vkCmdCopyImage(&g_ring,
                            remote_cb,
                            remote_src,
                            srcImageLayout,
                            remote_dst,
                            dstImageLayout,
                            regionCount,
                            pRegions);
    std::cout << "[Client ICD] vkCmdCopyImage recorded\n";
}

// vkCmdBlitImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageBlit* pRegions,
    VkFilter filter) {

    std::cout << "[Client ICD] vkCmdBlitImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBlitImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdBlitImage")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdBlitImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdBlitImage")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdBlitImage\n";
        return;
    }
    vn_async_vkCmdBlitImage(&g_ring,
                            remote_cb,
                            remote_src,
                            srcImageLayout,
                            remote_dst,
                            dstImageLayout,
                            regionCount,
                            pRegions,
                            filter);
    std::cout << "[Client ICD] vkCmdBlitImage recorded\n";
}

// vkCmdCopyBufferToImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {

    std::cout << "[Client ICD] vkCmdCopyBufferToImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBufferToImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyBufferToImage")) {
        return;
    }

    VkBuffer remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(srcBuffer, &remote_src, "vkCmdCopyBufferToImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdCopyBufferToImage")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdCopyBufferToImage\n";
        return;
    }
    vn_async_vkCmdCopyBufferToImage(&g_ring,
                                    remote_cb,
                                    remote_src,
                                    remote_dst,
                                    dstImageLayout,
                                    regionCount,
                                    pRegions);
    std::cout << "[Client ICD] vkCmdCopyBufferToImage recorded\n";
}

// vkCmdCopyImageToBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {

    std::cout << "[Client ICD] vkCmdCopyImageToBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImageToBuffer") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyImageToBuffer")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdCopyImageToBuffer") ||
        !ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdCopyImageToBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdCopyImageToBuffer\n";
        return;
    }
    vn_async_vkCmdCopyImageToBuffer(&g_ring,
                                    remote_cb,
                                    remote_src,
                                    srcImageLayout,
                                    remote_dst,
                                    regionCount,
                                    pRegions);
    std::cout << "[Client ICD] vkCmdCopyImageToBuffer recorded\n";
}

// vkCmdFillBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize size,
    uint32_t data) {

    std::cout << "[Client ICD] vkCmdFillBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdFillBuffer")) {
        return;
    }

    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdFillBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdFillBuffer\n";
        return;
    }
    vn_async_vkCmdFillBuffer(&g_ring, remote_cb, remote_dst, dstOffset, size, data);
    std::cout << "[Client ICD] vkCmdFillBuffer recorded\n";
}

// vkCmdUpdateBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize dataSize,
    const void* pData) {

    std::cout << "[Client ICD] vkCmdUpdateBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdUpdateBuffer")) {
        return;
    }

    if (!pData || dataSize == 0 || (dataSize % 4) != 0) {
        std::cerr << "[Client ICD] vkCmdUpdateBuffer requires 4-byte aligned data\n";
        return;
    }

    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdUpdateBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdUpdateBuffer\n";
        return;
    }
    vn_async_vkCmdUpdateBuffer(&g_ring, remote_cb, remote_dst, dstOffset, dataSize, pData);
    std::cout << "[Client ICD] vkCmdUpdateBuffer recorded\n";
}

// vkCmdClearColorImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const VkClearColorValue* pColor,
    uint32_t rangeCount,
    const VkImageSubresourceRange* pRanges) {

    std::cout << "[Client ICD] vkCmdClearColorImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdClearColorImage") ||
        !pColor ||
        !validate_buffer_regions(rangeCount, pRanges, "vkCmdClearColorImage")) {
        return;
    }

    VkImage remote_image = VK_NULL_HANDLE;
    if (!ensure_remote_image(image, &remote_image, "vkCmdClearColorImage")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdClearColorImage\n";
        return;
    }
    vn_async_vkCmdClearColorImage(&g_ring,
                                  remote_cb,
                                  remote_image,
                                  imageLayout,
                                  pColor,
                                  rangeCount,
                                  pRanges);
    std::cout << "[Client ICD] vkCmdClearColorImage recorded\n";
}

} // extern "C"
