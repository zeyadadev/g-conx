#include "icd_entrypoints.h"
#include "icd_instance.h"
#include "icd_device.h"
#include "network/network_client.h"
#include "state/handle_allocator.h"
#include "state/instance_state.h"
#include "state/device_state.h"
#include "vn_protocol_driver.h"
#include "vn_ring.h"
#include <algorithm>
#include <cstring>
#include <iostream>
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

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        // Still clean up local resources
        g_device_state.remove_device(device);
        delete icd_device;
        return;
    }

    // Call server to destroy device
    vn_async_vkDestroyDevice(&g_ring, icd_device->remote_handle, pAllocator);

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

} // extern "C"
