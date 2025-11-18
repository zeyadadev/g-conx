#include "icd_entrypoints.h"
#include "icd_instance.h"
#include "icd_device.h"
#include "network/network_client.h"
#include "state/handle_allocator.h"
#include "state/instance_state.h"
#include "state/device_state.h"
#include "state/resource_state.h"
#include "state/pipeline_state.h"
#include "state/shadow_buffer.h"
#include "state/command_buffer_state.h"
#include "state/sync_state.h"
#include "protocol/memory_transfer.h"
#include "vn_protocol_driver.h"
#include "vn_ring.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
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

static bool ensure_queue_tracked(VkQueue queue, VkQueue* remote_out) {
    if (!remote_out) {
        return false;
    }
    if (queue == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Queue handle is NULL\n";
        return false;
    }
    VkQueue remote_queue = g_device_state.get_remote_queue(queue);
    if (remote_queue == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Queue not tracked on client\n";
        return false;
    }
    *remote_out = remote_queue;
    return true;
}

static const VkSemaphoreTypeCreateInfo* find_semaphore_type_info(const VkSemaphoreCreateInfo* info) {
    if (!info) {
        return nullptr;
    }
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(info->pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            return reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

static bool check_payload_size(size_t payload_size) {
    if (payload_size > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "[Client ICD] Payload exceeds protocol limit (" << payload_size << " bytes)\n";
        return false;
    }
    return true;
}

static VkResult send_transfer_memory_data(VkDeviceMemory memory,
                                          VkDeviceSize offset,
                                          VkDeviceSize size,
                                          const void* data) {
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_memory == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Missing remote memory mapping for transfer\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == 0) {
        return VK_SUCCESS;
    }
    if (!data) {
        std::cerr << "[Client ICD] Transfer requested with null data pointer\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
        std::cerr << "[Client ICD] Transfer size exceeds host limits\n";
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    const size_t payload_size = sizeof(TransferMemoryDataHeader) + static_cast<size_t>(size);
    if (!check_payload_size(payload_size)) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    std::vector<uint8_t> payload(payload_size);
    TransferMemoryDataHeader header = {};
    header.command = VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA;
    header.memory_handle = reinterpret_cast<uint64_t>(remote_memory);
    header.offset = static_cast<uint64_t>(offset);
    header.size = static_cast<uint64_t>(size);

    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), data, static_cast<size_t>(size));

    if (!g_client.send(payload.data(), payload.size())) {
        std::cerr << "[Client ICD] Failed to send memory transfer message\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        std::cerr << "[Client ICD] Failed to receive memory transfer reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (reply.size() < sizeof(VkResult)) {
        std::cerr << "[Client ICD] Invalid reply size for memory transfer\n";
        return VK_ERROR_DEVICE_LOST;
    }

    VkResult result = VK_ERROR_DEVICE_LOST;
    std::memcpy(&result, reply.data(), sizeof(VkResult));
    return result;
}

static VkResult read_memory_data(VkDeviceMemory memory,
                                 VkDeviceSize offset,
                                 VkDeviceSize size,
                                 void* dst) {
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_memory == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Missing remote memory mapping for read\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == 0) {
        return VK_SUCCESS;
    }
    if (!dst) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
        std::cerr << "[Client ICD] Read size exceeds host limits\n";
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    ReadMemoryDataRequest request = {};
    request.command = VENUS_PLUS_CMD_READ_MEMORY_DATA;
    request.memory_handle = reinterpret_cast<uint64_t>(remote_memory);
    request.offset = static_cast<uint64_t>(offset);
    request.size = static_cast<uint64_t>(size);

    if (!check_payload_size(sizeof(request))) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (!g_client.send(&request, sizeof(request))) {
        std::cerr << "[Client ICD] Failed to send read memory request\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        std::cerr << "[Client ICD] Failed to receive read memory reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (reply.size() < sizeof(VkResult)) {
        std::cerr << "[Client ICD] Invalid reply for read memory request\n";
        return VK_ERROR_DEVICE_LOST;
    }

    VkResult result = VK_ERROR_DEVICE_LOST;
    std::memcpy(&result, reply.data(), sizeof(VkResult));
    if (result != VK_SUCCESS) {
        return result;
    }

    const size_t payload_size = reply.size() - sizeof(VkResult);
    if (payload_size != static_cast<size_t>(size)) {
        std::cerr << "[Client ICD] Read reply size mismatch (" << payload_size
                  << " vs " << size << ")\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::memcpy(dst, reply.data() + sizeof(VkResult), payload_size);
    return VK_SUCCESS;
}

static const VkTimelineSemaphoreSubmitInfo* find_timeline_submit_info(const void* pNext) {
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
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
    if (strcmp(pName, "vkCreateFence") == 0) {
        std::cout << " -> returning vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        std::cout << " -> returning vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        std::cout << " -> returning vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        std::cout << " -> returning vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        std::cout << " -> returning vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        std::cout << " -> returning vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        std::cout << " -> returning vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        std::cout << " -> returning vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        std::cout << " -> returning vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        std::cout << " -> returning vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        std::cout << " -> returning vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        std::cout << " -> returning vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        std::cout << " -> returning vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
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
    if (strcmp(pName, "vkMapMemory") == 0) {
        std::cout << " -> vkMapMemory\n";
        return (PFN_vkVoidFunction)vkMapMemory;
    }
    if (strcmp(pName, "vkUnmapMemory") == 0) {
        std::cout << " -> vkUnmapMemory\n";
        return (PFN_vkVoidFunction)vkUnmapMemory;
    }
    if (strcmp(pName, "vkFlushMappedMemoryRanges") == 0) {
        std::cout << " -> vkFlushMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkFlushMappedMemoryRanges;
    }
    if (strcmp(pName, "vkInvalidateMappedMemoryRanges") == 0) {
        std::cout << " -> vkInvalidateMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges;
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
    if (strcmp(pName, "vkCreateShaderModule") == 0) {
        std::cout << " -> vkCreateShaderModule\n";
        return (PFN_vkVoidFunction)vkCreateShaderModule;
    }
    if (strcmp(pName, "vkDestroyShaderModule") == 0) {
        std::cout << " -> vkDestroyShaderModule\n";
        return (PFN_vkVoidFunction)vkDestroyShaderModule;
    }
    if (strcmp(pName, "vkCreateDescriptorSetLayout") == 0) {
        std::cout << " -> vkCreateDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    }
    if (strcmp(pName, "vkDestroyDescriptorSetLayout") == 0) {
        std::cout << " -> vkDestroyDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    }
    if (strcmp(pName, "vkCreateDescriptorPool") == 0) {
        std::cout << " -> vkCreateDescriptorPool\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    }
    if (strcmp(pName, "vkDestroyDescriptorPool") == 0) {
        std::cout << " -> vkDestroyDescriptorPool\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    }
    if (strcmp(pName, "vkResetDescriptorPool") == 0) {
        std::cout << " -> vkResetDescriptorPool\n";
        return (PFN_vkVoidFunction)vkResetDescriptorPool;
    }
    if (strcmp(pName, "vkAllocateDescriptorSets") == 0) {
        std::cout << " -> vkAllocateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    }
    if (strcmp(pName, "vkFreeDescriptorSets") == 0) {
        std::cout << " -> vkFreeDescriptorSets\n";
        return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    }
    if (strcmp(pName, "vkUpdateDescriptorSets") == 0) {
        std::cout << " -> vkUpdateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    }
    if (strcmp(pName, "vkCreatePipelineLayout") == 0) {
        std::cout << " -> vkCreatePipelineLayout\n";
        return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    }
    if (strcmp(pName, "vkDestroyPipelineLayout") == 0) {
        std::cout << " -> vkDestroyPipelineLayout\n";
        return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    }
    if (strcmp(pName, "vkCreateComputePipelines") == 0) {
        std::cout << " -> vkCreateComputePipelines\n";
        return (PFN_vkVoidFunction)vkCreateComputePipelines;
    }
    if (strcmp(pName, "vkDestroyPipeline") == 0) {
        std::cout << " -> vkDestroyPipeline\n";
        return (PFN_vkVoidFunction)vkDestroyPipeline;
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
    if (strcmp(pName, "vkCmdBindPipeline") == 0) {
        std::cout << " -> vkCmdBindPipeline\n";
        return (PFN_vkVoidFunction)vkCmdBindPipeline;
    }
    if (strcmp(pName, "vkCmdBindDescriptorSets") == 0) {
        std::cout << " -> vkCmdBindDescriptorSets\n";
        return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    }
    if (strcmp(pName, "vkCmdDispatch") == 0) {
        std::cout << " -> vkCmdDispatch\n";
        return (PFN_vkVoidFunction)vkCmdDispatch;
    }
    if (strcmp(pName, "vkCmdPipelineBarrier") == 0) {
        std::cout << " -> vkCmdPipelineBarrier\n";
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    }
    if (strcmp(pName, "vkCreateFence") == 0) {
        std::cout << " -> vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        std::cout << " -> vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        std::cout << " -> vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        std::cout << " -> vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        std::cout << " -> vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        std::cout << " -> vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        std::cout << " -> vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        std::cout << " -> vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        std::cout << " -> vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        std::cout << " -> vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        std::cout << " -> vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        std::cout << " -> vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        std::cout << " -> vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
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
        g_pipeline_state.remove_device_resources(device);
        g_sync_state.remove_device(device);
        g_shadow_buffer_manager.remove_device(device);
        g_device_state.remove_device(device);
        delete icd_device;
        return;
    }

    // Call server to destroy device
    vn_async_vkDestroyDevice(&g_ring, icd_device->remote_handle, pAllocator);

    // Drop resource tracking for this device
    g_resource_state.remove_device_resources(device);
    g_pipeline_state.remove_device_resources(device);
    g_sync_state.remove_device(device);
    g_shadow_buffer_manager.remove_device(device);

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

    // Call server to get queue (synchronous so we can track remote handle)
    vn_call_vkGetDeviceQueue(&g_ring, icd_device->remote_handle, queueFamilyIndex, queueIndex, &icd_queue->remote_handle);

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

    ShadowBufferMapping mapping = {};
    if (g_shadow_buffer_manager.remove_mapping(memory, &mapping)) {
        if (mapping.data) {
            std::free(mapping.data);
        }
        std::cerr << "[Client ICD] Warning: Memory freed while still mapped, dropping local shadow buffer\n";
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

// vkMapMemory - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void** ppData) {

    std::cout << "[Client ICD] vkMapMemory called\n";

    if (!ppData) {
        std::cerr << "[Client ICD] vkMapMemory requires valid ppData\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    *ppData = nullptr;

    if (flags != 0) {
        std::cerr << "[Client ICD] vkMapMemory flags must be zero (got " << flags << ")\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkMapMemory\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (!g_device_state.has_device(device) || !g_resource_state.has_memory(memory)) {
        std::cerr << "[Client ICD] vkMapMemory called with unknown device or memory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (g_shadow_buffer_manager.is_mapped(memory)) {
        std::cerr << "[Client ICD] Memory already mapped\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkDevice memory_device = g_resource_state.get_memory_device(memory);
    if (memory_device != device) {
        std::cerr << "[Client ICD] Memory belongs to different device\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
    if (size == VK_WHOLE_SIZE) {
        if (offset >= memory_size) {
            std::cerr << "[Client ICD] vkMapMemory offset beyond allocation size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        size = memory_size - offset;
    }

    if (offset + size > memory_size) {
        std::cerr << "[Client ICD] vkMapMemory range exceeds allocation (offset=" << offset
                  << ", size=" << size << ", alloc=" << memory_size << ")\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    DeviceEntry* device_entry = g_device_state.get_device(device);
    if (!device_entry) {
        std::cerr << "[Client ICD] Failed to find device entry during vkMapMemory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkPhysicalDeviceMemoryProperties mem_props = {};
    vkGetPhysicalDeviceMemoryProperties(device_entry->physical_device, &mem_props);

    uint32_t type_index = g_resource_state.get_memory_type_index(memory);
    if (type_index >= mem_props.memoryTypeCount) {
        std::cerr << "[Client ICD] Invalid memory type index during vkMapMemory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkMemoryPropertyFlags property_flags = mem_props.memoryTypes[type_index].propertyFlags;
    if ((property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        std::cerr << "[Client ICD] Memory type is not HOST_VISIBLE\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    bool host_coherent = (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    void* shadow_ptr = nullptr;
    if (!g_shadow_buffer_manager.create_mapping(device, memory, offset, size, host_coherent, &shadow_ptr)) {
        std::cerr << "[Client ICD] Failed to allocate shadow buffer for mapping\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkResult read_result = read_memory_data(memory, offset, size, shadow_ptr);
    if (read_result != VK_SUCCESS) {
        ShadowBufferMapping mapping = {};
        g_shadow_buffer_manager.remove_mapping(memory, &mapping);
        if (mapping.data) {
            std::free(mapping.data);
        }
        return read_result;
    }

    *ppData = shadow_ptr;
    std::cout << "[Client ICD] Memory mapped (size=" << size << ", offset=" << offset << ")\n";
    return VK_SUCCESS;
}

// vkUnmapMemory - Phase 8
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory) {

    std::cout << "[Client ICD] vkUnmapMemory called\n";

    if (memory == VK_NULL_HANDLE) {
        return;
    }

    ShadowBufferMapping mapping = {};
    if (!g_shadow_buffer_manager.remove_mapping(memory, &mapping)) {
        std::cerr << "[Client ICD] vkUnmapMemory: memory was not mapped\n";
        return;
    }

    if (mapping.device != device) {
        std::cerr << "[Client ICD] vkUnmapMemory: device mismatch\n";
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Lost connection before flushing vkUnmapMemory\n";
        if (mapping.data) {
            std::free(mapping.data);
        }
        return;
    }

    if (mapping.size > 0 && mapping.data) {
        VkResult result = send_transfer_memory_data(memory, mapping.offset, mapping.size, mapping.data);
        if (result != VK_SUCCESS) {
            std::cerr << "[Client ICD] Failed to transfer memory on unmap: " << result << "\n";
        } else {
            std::cout << "[Client ICD] Transferred " << mapping.size << " bytes on unmap\n";
        }
    }

    if (mapping.data) {
        std::free(mapping.data);
    }
}

// vkFlushMappedMemoryRanges - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    std::cout << "[Client ICD] vkFlushMappedMemoryRanges called (count=" << memoryRangeCount << ")\n";

    if (memoryRangeCount == 0) {
        return VK_SUCCESS;
    }
    if (!pMemoryRanges) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        return VK_ERROR_DEVICE_LOST;
    }

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const VkMappedMemoryRange& range = pMemoryRanges[i];
        ShadowBufferMapping mapping = {};
        if (!g_shadow_buffer_manager.get_mapping(range.memory, &mapping)) {
            std::cerr << "[Client ICD] vkFlushMappedMemoryRanges: memory not mapped\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (mapping.device != device) {
            std::cerr << "[Client ICD] vkFlushMappedMemoryRanges: device mismatch\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (range.offset < mapping.offset) {
            std::cerr << "[Client ICD] vkFlushMappedMemoryRanges: offset before mapping\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize relative_offset = range.offset - mapping.offset;
        if (relative_offset > mapping.size) {
            std::cerr << "[Client ICD] vkFlushMappedMemoryRanges: offset beyond mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize flush_size = range.size;
        if (flush_size == VK_WHOLE_SIZE) {
            flush_size = mapping.size - relative_offset;
        }
        if (relative_offset + flush_size > mapping.size) {
            std::cerr << "[Client ICD] vkFlushMappedMemoryRanges: range exceeds mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (flush_size == 0) {
            continue;
        }

        const uint8_t* src = static_cast<const uint8_t*>(mapping.data);
        VkResult result = send_transfer_memory_data(range.memory,
                                                    range.offset,
                                                    flush_size,
                                                    src + static_cast<size_t>(relative_offset));
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

// vkInvalidateMappedMemoryRanges - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    std::cout << "[Client ICD] vkInvalidateMappedMemoryRanges called (count=" << memoryRangeCount << ")\n";

    if (memoryRangeCount == 0) {
        return VK_SUCCESS;
    }
    if (!pMemoryRanges) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        return VK_ERROR_DEVICE_LOST;
    }

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const VkMappedMemoryRange& range = pMemoryRanges[i];
        ShadowBufferMapping mapping = {};
        if (!g_shadow_buffer_manager.get_mapping(range.memory, &mapping)) {
            std::cerr << "[Client ICD] vkInvalidateMappedMemoryRanges: memory not mapped\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (mapping.device != device) {
            std::cerr << "[Client ICD] vkInvalidateMappedMemoryRanges: device mismatch\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (range.offset < mapping.offset) {
            std::cerr << "[Client ICD] vkInvalidateMappedMemoryRanges: offset before mapping\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize relative_offset = range.offset - mapping.offset;
        if (relative_offset > mapping.size) {
            std::cerr << "[Client ICD] vkInvalidateMappedMemoryRanges: offset beyond mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize read_size = range.size;
        if (read_size == VK_WHOLE_SIZE) {
            read_size = mapping.size - relative_offset;
        }
        if (relative_offset + read_size > mapping.size) {
            std::cerr << "[Client ICD] vkInvalidateMappedMemoryRanges: range exceeds mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (read_size == 0) {
            continue;
        }

        uint8_t* dst = static_cast<uint8_t*>(mapping.data);
        VkResult result = read_memory_data(range.memory,
                                           range.offset,
                                           read_size,
                                           dst + static_cast<size_t>(relative_offset));
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
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

// vkCreateShaderModule - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkShaderModule* pShaderModule) {

    std::cout << "[Client ICD] vkCreateShaderModule called\n";

    if (!pCreateInfo || !pShaderModule || !pCreateInfo->pCode || pCreateInfo->codeSize == 0 ||
        (pCreateInfo->codeSize % 4) != 0) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateShaderModule\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateShaderModule\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkShaderModule remote_module = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateShaderModule(&g_ring,
                                                   icd_device->remote_handle,
                                                   pCreateInfo,
                                                   pAllocator,
                                                   &remote_module);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateShaderModule failed: " << result << "\n";
        return result;
    }

    VkShaderModule local = g_handle_allocator.allocate<VkShaderModule>();
    g_pipeline_state.add_shader_module(device, local, remote_module, pCreateInfo->codeSize);
    *pShaderModule = local;

    std::cout << "[Client ICD] Shader module created (local=" << local
              << ", remote=" << remote_module << ")\n";
    return VK_SUCCESS;
}

// vkDestroyShaderModule - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device,
    VkShaderModule shaderModule,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyShaderModule called\n";

    if (shaderModule == VK_NULL_HANDLE) {
        return;
    }

    VkShaderModule remote_module = g_pipeline_state.get_remote_shader_module(shaderModule);
    g_pipeline_state.remove_shader_module(shaderModule);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyShaderModule\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyShaderModule\n";
        return;
    }

    if (remote_module == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Missing remote shader module handle\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyShaderModule(&g_ring, icd_device->remote_handle, remote_module, pAllocator);
    std::cout << "[Client ICD] Shader module destroyed (local=" << shaderModule << ")\n";
}

// vkCreateDescriptorSetLayout - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorSetLayout* pSetLayout) {

    std::cout << "[Client ICD] vkCreateDescriptorSetLayout called\n";

    if (!pCreateInfo || !pSetLayout) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateDescriptorSetLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateDescriptorSetLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDescriptorSetLayout remote_layout = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateDescriptorSetLayout(&g_ring,
                                                          icd_device->remote_handle,
                                                          pCreateInfo,
                                                          pAllocator,
                                                          &remote_layout);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateDescriptorSetLayout failed: " << result << "\n";
        return result;
    }

    VkDescriptorSetLayout local = g_handle_allocator.allocate<VkDescriptorSetLayout>();
    g_pipeline_state.add_descriptor_set_layout(device, local, remote_layout);
    *pSetLayout = local;
    std::cout << "[Client ICD] Descriptor set layout created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDescriptorSetLayout - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device,
    VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyDescriptorSetLayout called\n";

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorSetLayout remote_layout =
        g_pipeline_state.get_remote_descriptor_set_layout(descriptorSetLayout);
    g_pipeline_state.remove_descriptor_set_layout(descriptorSetLayout);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (remote_layout == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote descriptor set layout handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorSetLayout(&g_ring,
                                          icd_device->remote_handle,
                                          remote_layout,
                                          pAllocator);
    std::cout << "[Client ICD] Descriptor set layout destroyed (local=" << descriptorSetLayout
              << ")\n";
}

// vkCreateDescriptorPool - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device,
    const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorPool* pDescriptorPool) {

    std::cout << "[Client ICD] vkCreateDescriptorPool called\n";

    if (!pCreateInfo || !pDescriptorPool) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDescriptorPool remote_pool = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateDescriptorPool(&g_ring,
                                                     icd_device->remote_handle,
                                                     pCreateInfo,
                                                     pAllocator,
                                                     &remote_pool);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateDescriptorPool failed: " << result << "\n";
        return result;
    }

    VkDescriptorPool local = g_handle_allocator.allocate<VkDescriptorPool>();
    g_pipeline_state.add_descriptor_pool(device, local, remote_pool, pCreateInfo->flags);
    *pDescriptorPool = local;
    std::cout << "[Client ICD] Descriptor pool created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDescriptorPool - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    g_pipeline_state.remove_descriptor_pool(descriptorPool);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyDescriptorPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyDescriptorPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote descriptor pool handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorPool(&g_ring,
                                     icd_device->remote_handle,
                                     remote_pool,
                                     pAllocator);
    std::cout << "[Client ICD] Descriptor pool destroyed (local=" << descriptorPool << ")\n";
}

// vkResetDescriptorPool - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    VkDescriptorPoolResetFlags flags) {

    std::cout << "[Client ICD] vkResetDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkResetDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetDescriptorPool(&g_ring,
                                                    icd_device->remote_handle,
                                                    remote_pool,
                                                    flags);
    if (result == VK_SUCCESS) {
        g_pipeline_state.reset_descriptor_pool(descriptorPool);
    } else {
        std::cerr << "[Client ICD] vkResetDescriptorPool failed: " << result << "\n";
    }
    return result;
}

// vkAllocateDescriptorSets - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device,
    const VkDescriptorSetAllocateInfo* pAllocateInfo,
    VkDescriptorSet* pDescriptorSets) {

    std::cout << "[Client ICD] vkAllocateDescriptorSets called\n";

    if (!pAllocateInfo || (!pDescriptorSets && pAllocateInfo->descriptorSetCount > 0)) {
        std::cerr << "[Client ICD] Invalid parameters for vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pAllocateInfo->descriptorSetCount == 0) {
        return VK_SUCCESS;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pAllocateInfo->pSetLayouts) {
        std::cerr << "[Client ICD] Layout array missing in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool =
        g_pipeline_state.get_remote_descriptor_pool(pAllocateInfo->descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSetLayout> remote_layouts(pAllocateInfo->descriptorSetCount);
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        remote_layouts[i] =
            g_pipeline_state.get_remote_descriptor_set_layout(pAllocateInfo->pSetLayouts[i]);
        if (remote_layouts[i] == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Descriptor set layout not tracked for allocation\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkDescriptorSetAllocateInfo remote_info = *pAllocateInfo;
    remote_info.descriptorPool = remote_pool;
    remote_info.pSetLayouts = remote_layouts.data();

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkDescriptorSet> remote_sets(pAllocateInfo->descriptorSetCount);
    VkResult result = vn_call_vkAllocateDescriptorSets(&g_ring,
                                                       icd_device->remote_handle,
                                                       &remote_info,
                                                       remote_sets.data());
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkAllocateDescriptorSets failed: " << result << "\n";
        return result;
    }

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        VkDescriptorSet local = g_handle_allocator.allocate<VkDescriptorSet>();
        g_pipeline_state.add_descriptor_set(device,
                                            pAllocateInfo->descriptorPool,
                                            pAllocateInfo->pSetLayouts[i],
                                            local,
                                            remote_sets[i]);
        pDescriptorSets[i] = local;
    }

    std::cout << "[Client ICD] Allocated " << pAllocateInfo->descriptorSetCount
              << " descriptor set(s)\n";
    return VK_SUCCESS;
}

// vkFreeDescriptorSets - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets) {

    std::cout << "[Client ICD] vkFreeDescriptorSets called (count=" << descriptorSetCount << ")\n";

    if (descriptorSetCount == 0) {
        return VK_SUCCESS;
    }
    if (!pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkFreeDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSet> remote_sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        remote_sets[i] = g_pipeline_state.get_remote_descriptor_set(pDescriptorSets[i]);
        if (remote_sets[i] == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Descriptor set not tracked during free\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkFreeDescriptorSets(&g_ring,
                                                   icd_device->remote_handle,
                                                   remote_pool,
                                                   descriptorSetCount,
                                                   remote_sets.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < descriptorSetCount; ++i) {
            g_pipeline_state.remove_descriptor_set(pDescriptorSets[i]);
        }
        std::cout << "[Client ICD] Freed " << descriptorSetCount << " descriptor set(s)\n";
    } else {
        std::cerr << "[Client ICD] vkFreeDescriptorSets failed: " << result << "\n";
    }
    return result;
}

// vkUpdateDescriptorSets - Phase 9
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet* pDescriptorCopies) {

    std::cout << "[Client ICD] vkUpdateDescriptorSets called (writes=" << descriptorWriteCount
              << ", copies=" << descriptorCopyCount << ")\n";

    if (descriptorWriteCount == 0 && descriptorCopyCount == 0) {
        return;
    }

    if ((!pDescriptorWrites && descriptorWriteCount > 0) ||
        (!pDescriptorCopies && descriptorCopyCount > 0)) {
        std::cerr << "[Client ICD] Invalid descriptor write/copy arrays\n";
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkUpdateDescriptorSets\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkWriteDescriptorSet> remote_writes(descriptorWriteCount);
    std::vector<std::vector<VkDescriptorBufferInfo>> buffer_infos(descriptorWriteCount);

    auto convert_buffer_info = [](VkDescriptorType type) -> bool {
        switch (type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return true;
        default:
            return false;
        }
    };

    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet& src = pDescriptorWrites[i];
        VkWriteDescriptorSet& dst = remote_writes[i];
        dst = src;
        dst.dstSet = g_pipeline_state.get_remote_descriptor_set(src.dstSet);
        if (dst.dstSet == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Descriptor set not tracked in vkUpdateDescriptorSets\n";
            return;
        }

        if (convert_buffer_info(src.descriptorType)) {
            if (!src.pBufferInfo) {
                std::cerr << "[Client ICD] Missing buffer info for descriptor update\n";
                return;
            }
            buffer_infos[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                buffer_infos[i][j] = src.pBufferInfo[j];
                buffer_infos[i][j].buffer =
                    g_resource_state.get_remote_buffer(src.pBufferInfo[j].buffer);
                if (buffer_infos[i][j].buffer == VK_NULL_HANDLE) {
                    std::cerr << "[Client ICD] Buffer not tracked for descriptor update\n";
                    return;
                }
            }
            dst.pBufferInfo = buffer_infos[i].data();
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = nullptr;
        } else if (src.descriptorCount > 0) {
            std::cerr << "[Client ICD] Unsupported descriptor type in vkUpdateDescriptorSets\n";
            return;
        }
    }

    std::vector<VkCopyDescriptorSet> remote_copies(descriptorCopyCount);
    for (uint32_t i = 0; i < descriptorCopyCount; ++i) {
        remote_copies[i] = pDescriptorCopies[i];
        remote_copies[i].srcSet =
            g_pipeline_state.get_remote_descriptor_set(pDescriptorCopies[i].srcSet);
        remote_copies[i].dstSet =
            g_pipeline_state.get_remote_descriptor_set(pDescriptorCopies[i].dstSet);
        if (remote_copies[i].srcSet == VK_NULL_HANDLE ||
            remote_copies[i].dstSet == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Descriptor set not tracked for copy update\n";
            return;
        }
    }

    vn_async_vkUpdateDescriptorSets(&g_ring,
                                    icd_device->remote_handle,
                                    descriptorWriteCount,
                                    remote_writes.data(),
                                    descriptorCopyCount,
                                    remote_copies.data());
    std::cout << "[Client ICD] Descriptor sets updated\n";
}

// vkCreatePipelineLayout - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device,
    const VkPipelineLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkPipelineLayout* pPipelineLayout) {

    std::cout << "[Client ICD] vkCreatePipelineLayout called\n";

    if (!pCreateInfo || !pPipelineLayout) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreatePipelineLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreatePipelineLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSetLayout> remote_layouts;
    if (pCreateInfo->setLayoutCount > 0) {
        remote_layouts.resize(pCreateInfo->setLayoutCount);
        for (uint32_t i = 0; i < pCreateInfo->setLayoutCount; ++i) {
            remote_layouts[i] =
                g_pipeline_state.get_remote_descriptor_set_layout(pCreateInfo->pSetLayouts[i]);
            if (remote_layouts[i] == VK_NULL_HANDLE) {
                std::cerr << "[Client ICD] Descriptor set layout not tracked for pipeline layout\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }

    VkPipelineLayoutCreateInfo remote_info = *pCreateInfo;
    if (!remote_layouts.empty()) {
        remote_info.pSetLayouts = remote_layouts.data();
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkPipelineLayout remote_layout = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreatePipelineLayout(&g_ring,
                                                     icd_device->remote_handle,
                                                     &remote_info,
                                                     pAllocator,
                                                     &remote_layout);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreatePipelineLayout failed: " << result << "\n";
        return result;
    }

    VkPipelineLayout local = g_handle_allocator.allocate<VkPipelineLayout>();
    g_pipeline_state.add_pipeline_layout(device, local, remote_layout);
    *pPipelineLayout = local;
    std::cout << "[Client ICD] Pipeline layout created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyPipelineLayout - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device,
    VkPipelineLayout pipelineLayout,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyPipelineLayout called\n";

    if (pipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    VkPipelineLayout remote_layout =
        g_pipeline_state.get_remote_pipeline_layout(pipelineLayout);
    g_pipeline_state.remove_pipeline_layout(pipelineLayout);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyPipelineLayout\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyPipelineLayout\n";
        return;
    }

    if (remote_layout == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote pipeline layout handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyPipelineLayout(&g_ring,
                                     icd_device->remote_handle,
                                     remote_layout,
                                     pAllocator);
    std::cout << "[Client ICD] Pipeline layout destroyed (local=" << pipelineLayout << ")\n";
}

// vkCreateComputePipelines - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {

    std::cout << "[Client ICD] vkCreateComputePipelines called (count=" << createInfoCount << ")\n";

    if (!pCreateInfos || (!pPipelines && createInfoCount > 0)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (createInfoCount == 0) {
        return VK_SUCCESS;
    }

    if (pipelineCache != VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Pipeline cache not supported in Phase 9\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateComputePipelines\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkComputePipelineCreateInfo> remote_infos(createInfoCount);
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        remote_infos[i] = pCreateInfos[i];
        VkShaderModule remote_module =
            g_pipeline_state.get_remote_shader_module(pCreateInfos[i].stage.module);
        if (remote_module == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Shader module not tracked for compute pipeline\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_infos[i].stage.module = remote_module;

        VkPipelineLayout remote_layout =
            g_pipeline_state.get_remote_pipeline_layout(pCreateInfos[i].layout);
        if (remote_layout == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Pipeline layout not tracked for compute pipeline\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_infos[i].layout = remote_layout;

        if (pCreateInfos[i].basePipelineHandle != VK_NULL_HANDLE) {
            VkPipeline remote_base =
                g_pipeline_state.get_remote_pipeline(pCreateInfos[i].basePipelineHandle);
            if (remote_base == VK_NULL_HANDLE) {
                std::cerr << "[Client ICD] Base pipeline not tracked for compute pipeline\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            remote_infos[i].basePipelineHandle = remote_base;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkPipeline> remote_pipelines(createInfoCount, VK_NULL_HANDLE);
    VkResult result = vn_call_vkCreateComputePipelines(&g_ring,
                                                       icd_device->remote_handle,
                                                       pipelineCache,
                                                       createInfoCount,
                                                       remote_infos.data(),
                                                       pAllocator,
                                                       remote_pipelines.data());
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateComputePipelines failed: " << result << "\n";
        return result;
    }

    for (uint32_t i = 0; i < createInfoCount; ++i) {
        VkPipeline local = g_handle_allocator.allocate<VkPipeline>();
        g_pipeline_state.add_pipeline(device,
                                      VK_PIPELINE_BIND_POINT_COMPUTE,
                                      local,
                                      remote_pipelines[i]);
        pPipelines[i] = local;
    }

    std::cout << "[Client ICD] Compute pipeline(s) created\n";
    return VK_SUCCESS;
}

// vkDestroyPipeline - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice device,
    VkPipeline pipeline,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyPipeline called\n";

    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    VkPipeline remote_pipeline = g_pipeline_state.get_remote_pipeline(pipeline);
    g_pipeline_state.remove_pipeline(pipeline);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyPipeline\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyPipeline\n";
        return;
    }

    if (remote_pipeline == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote pipeline handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyPipeline(&g_ring,
                               icd_device->remote_handle,
                               remote_pipeline,
                               pAllocator);
    std::cout << "[Client ICD] Pipeline destroyed (local=" << pipeline << ")\n";
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

// vkCmdBindPipeline - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipeline pipeline) {

    std::cout << "[Client ICD] vkCmdBindPipeline called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBindPipeline")) {
        return;
    }

    if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        std::cerr << "[Client ICD] Only compute bind point supported in vkCmdBindPipeline\n";
        return;
    }

    VkPipeline remote_pipeline = g_pipeline_state.get_remote_pipeline(pipeline);
    if (remote_pipeline == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Pipeline not tracked in vkCmdBindPipeline\n";
        return;
    }

    VkPipelineBindPoint stored_bind_point = g_pipeline_state.get_pipeline_bind_point(pipeline);
    if (stored_bind_point != pipelineBindPoint) {
        std::cerr << "[Client ICD] Pipeline bind point mismatch\n";
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdBindPipeline\n";
        return;
    }

    vn_async_vkCmdBindPipeline(&g_ring, remote_cb, pipelineBindPoint, remote_pipeline);
    std::cout << "[Client ICD] Compute pipeline bound\n";
}

// vkCmdBindDescriptorSets - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t* pDynamicOffsets) {

    std::cout << "[Client ICD] vkCmdBindDescriptorSets called (count=" << descriptorSetCount << ")\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBindDescriptorSets")) {
        return;
    }

    if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        std::cerr << "[Client ICD] Only compute bind point supported in vkCmdBindDescriptorSets\n";
        return;
    }

    if (descriptorSetCount > 0 && !pDescriptorSets) {
        std::cerr << "[Client ICD] Descriptor set array missing in vkCmdBindDescriptorSets\n";
        return;
    }

    VkPipelineLayout remote_layout = g_pipeline_state.get_remote_pipeline_layout(layout);
    if (remote_layout == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Pipeline layout not tracked in vkCmdBindDescriptorSets\n";
        return;
    }

    std::vector<VkDescriptorSet> remote_sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        remote_sets[i] = g_pipeline_state.get_remote_descriptor_set(pDescriptorSets[i]);
        if (remote_sets[i] == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Descriptor set not tracked in vkCmdBindDescriptorSets\n";
            return;
        }
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdBindDescriptorSets\n";
        return;
    }

    vn_async_vkCmdBindDescriptorSets(&g_ring,
                                     remote_cb,
                                     pipelineBindPoint,
                                     remote_layout,
                                     firstSet,
                                     descriptorSetCount,
                                     remote_sets.empty() ? nullptr : remote_sets.data(),
                                     dynamicOffsetCount,
                                     pDynamicOffsets);
    std::cout << "[Client ICD] Descriptor sets bound\n";
}

// vkCmdDispatch - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer commandBuffer,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ) {

    std::cout << "[Client ICD] vkCmdDispatch called ("
              << groupCountX << ", " << groupCountY << ", " << groupCountZ << ")\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdDispatch")) {
        return;
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdDispatch\n";
        return;
    }

    vn_async_vkCmdDispatch(&g_ring, remote_cb, groupCountX, groupCountY, groupCountZ);
    std::cout << "[Client ICD] Dispatch recorded\n";
}

// vkCmdPipelineBarrier - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount,
    const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier* pImageMemoryBarriers) {

    std::cout << "[Client ICD] vkCmdPipelineBarrier called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPipelineBarrier")) {
        return;
    }

    if ((memoryBarrierCount > 0 && !pMemoryBarriers) ||
        (bufferMemoryBarrierCount > 0 && !pBufferMemoryBarriers) ||
        (imageMemoryBarrierCount > 0 && !pImageMemoryBarriers)) {
        std::cerr << "[Client ICD] Invalid barrier arrays\n";
        return;
    }

    std::vector<VkBufferMemoryBarrier> buffer_barriers(bufferMemoryBarrierCount);
    for (uint32_t i = 0; i < bufferMemoryBarrierCount; ++i) {
        buffer_barriers[i] = pBufferMemoryBarriers[i];
        buffer_barriers[i].buffer =
            g_resource_state.get_remote_buffer(pBufferMemoryBarriers[i].buffer);
        if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Buffer not tracked in vkCmdPipelineBarrier\n";
            return;
        }
    }

    std::vector<VkImageMemoryBarrier> image_barriers(imageMemoryBarrierCount);
    for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
        image_barriers[i] = pImageMemoryBarriers[i];
        image_barriers[i].image =
            g_resource_state.get_remote_image(pImageMemoryBarriers[i].image);
        if (image_barriers[i].image == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] Image not tracked in vkCmdPipelineBarrier\n";
            return;
        }
    }

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote command buffer missing in vkCmdPipelineBarrier\n";
        return;
    }

    vn_async_vkCmdPipelineBarrier(&g_ring,
                                  remote_cb,
                                  srcStageMask,
                                  dstStageMask,
                                  dependencyFlags,
                                  memoryBarrierCount,
                                  pMemoryBarriers,
                                  bufferMemoryBarrierCount,
                                  buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
                                  imageMemoryBarrierCount,
                                  image_barriers.empty() ? nullptr : image_barriers.data());
    std::cout << "[Client ICD] Pipeline barrier recorded\n";
}

// Synchronization objects - Phase 6
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFence* pFence) {

    std::cout << "[Client ICD] vkCreateFence called\n";

    if (!pCreateInfo || !pFence) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkFence remote_fence = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateFence(&g_ring, icd_device->remote_handle, pCreateInfo, pAllocator, &remote_fence);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateFence failed: " << result << "\n";
        return result;
    }

    VkFence local_fence = g_handle_allocator.allocate<VkFence>();
    g_sync_state.add_fence(device, local_fence, remote_fence, (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
    *pFence = local_fence;
    std::cout << "[Client ICD] Fence created (local=" << *pFence << ", remote=" << remote_fence << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroyFence called\n";
    if (fence == VK_NULL_HANDLE) {
        return;
    }

    VkFence remote = g_sync_state.get_remote_fence(fence);
    g_sync_state.remove_fence(fence);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroyFence\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroyFence\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote fence missing in vkDestroyFence\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyFence(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence) {
    std::cout << "[Client ICD] vkGetFenceStatus called\n";
    if (!g_sync_state.has_fence(fence)) {
        std::cerr << "[Client ICD] Unknown fence in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkFence remote = g_sync_state.get_remote_fence(fence);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetFenceStatus(&g_ring, icd_device->remote_handle, remote);
    if (result == VK_SUCCESS) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences) {

    std::cout << "[Client ICD] vkResetFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkResetFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] vkResetFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetFences(&g_ring,
                                            icd_device->remote_handle,
                                            fenceCount,
                                            remote_fences.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < fenceCount; ++i) {
            g_sync_state.set_fence_signaled(pFences[i], false);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout) {

    std::cout << "[Client ICD] vkWaitForFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkWaitForFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] vkWaitForFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkWaitForFences(&g_ring,
                                              icd_device->remote_handle,
                                              fenceCount,
                                              remote_fences.data(),
                                              waitAll,
                                              timeout);
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < fenceCount; ++i) {
            g_sync_state.set_fence_signaled(pFences[i], true);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore) {

    std::cout << "[Client ICD] vkCreateSemaphore called\n";

    if (!pCreateInfo || !pSemaphore) {
        std::cerr << "[Client ICD] Invalid parameters for vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSemaphore remote_semaphore = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateSemaphore(&g_ring,
                                                icd_device->remote_handle,
                                                pCreateInfo,
                                                pAllocator,
                                                &remote_semaphore);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkCreateSemaphore failed: " << result << "\n";
        return result;
    }

    const VkSemaphoreTypeCreateInfo* type_info = find_semaphore_type_info(pCreateInfo);
    VkSemaphoreType type = type_info ? type_info->semaphoreType : VK_SEMAPHORE_TYPE_BINARY;
    uint64_t initial_value = type_info ? type_info->initialValue : 0;

    VkSemaphore local_semaphore = g_handle_allocator.allocate<VkSemaphore>();
    g_sync_state.add_semaphore(device,
                               local_semaphore,
                               remote_semaphore,
                               type,
                               false,
                               initial_value);
    *pSemaphore = local_semaphore;
    std::cout << "[Client ICD] Semaphore created (local=" << *pSemaphore
              << ", remote=" << remote_semaphore
              << ", type=" << (type == VK_SEMAPHORE_TYPE_TIMELINE ? "timeline" : "binary") << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator) {

    std::cout << "[Client ICD] vkDestroySemaphore called\n";
    if (semaphore == VK_NULL_HANDLE) {
        return;
    }

    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    g_sync_state.remove_semaphore(semaphore);

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server during vkDestroySemaphore\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDestroySemaphore\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        std::cerr << "[Client ICD] Remote semaphore missing in vkDestroySemaphore\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroySemaphore(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(
    VkDevice device,
    VkSemaphore semaphore,
    uint64_t* pValue) {

    std::cout << "[Client ICD] vkGetSemaphoreCounterValue called\n";

    if (!pValue) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_sync_state.has_semaphore(semaphore)) {
        std::cerr << "[Client ICD] Unknown semaphore in vkGetSemaphoreCounterValue\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (g_sync_state.get_semaphore_type(semaphore) != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkGetSemaphoreCounterValue\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetSemaphoreCounterValue(&g_ring,
                                                         icd_device->remote_handle,
                                                         remote,
                                                         pValue);
    if (result == VK_SUCCESS) {
        g_sync_state.set_timeline_value(semaphore, *pValue);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(
    VkDevice device,
    const VkSemaphoreSignalInfo* pSignalInfo) {

    std::cout << "[Client ICD] vkSignalSemaphore called\n";

    if (!pSignalInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSemaphore semaphore = pSignalInfo->semaphore;
    if (!g_sync_state.has_semaphore(semaphore)) {
        std::cerr << "[Client ICD] Unknown semaphore in vkSignalSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (g_sync_state.get_semaphore_type(semaphore) != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkSignalSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSemaphoreSignalInfo remote_info = *pSignalInfo;
    remote_info.semaphore = remote;
    VkResult result = vn_call_vkSignalSemaphore(&g_ring, icd_device->remote_handle, &remote_info);
    if (result == VK_SUCCESS) {
        g_sync_state.set_timeline_value(semaphore, pSignalInfo->value);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(
    VkDevice device,
    const VkSemaphoreWaitInfo* pWaitInfo,
    uint64_t timeout) {

    std::cout << "[Client ICD] vkWaitSemaphores called\n";

    if (!pWaitInfo || pWaitInfo->semaphoreCount == 0 ||
        !pWaitInfo->pSemaphores || !pWaitInfo->pValues) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkWaitSemaphores\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkSemaphore> remote_handles(pWaitInfo->semaphoreCount);
    for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
        VkSemaphore sem = pWaitInfo->pSemaphores[i];
        if (!g_sync_state.has_semaphore(sem) ||
            g_sync_state.get_semaphore_type(sem) != VK_SEMAPHORE_TYPE_TIMELINE) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        VkSemaphore remote = g_sync_state.get_remote_semaphore(sem);
        if (remote == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_handles[i] = remote;
    }

    VkSemaphoreWaitInfo remote_info = *pWaitInfo;
    remote_info.pSemaphores = remote_handles.data();

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkWaitSemaphores(&g_ring, icd_device->remote_handle, &remote_info, timeout);
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
            g_sync_state.set_timeline_value(pWaitInfo->pSemaphores[i], pWaitInfo->pValues[i]);
        }
    }
    return result;
}

// Queue submission - Phase 6
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {

    std::cout << "[Client ICD] vkQueueSubmit called (submitCount=" << submitCount << ")\n";

    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            std::cerr << "[Client ICD] vkQueueSubmit: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    struct SubmitStorage {
        std::vector<VkSemaphore> wait_local;
        std::vector<VkSemaphore> signal_local;
        std::vector<VkSemaphore> wait_remote;
        std::vector<VkPipelineStageFlags> wait_stages;
        std::vector<VkCommandBuffer> remote_cbs;
        std::vector<VkSemaphore> signal_remote;
        std::vector<uint64_t> wait_values;
        std::vector<uint64_t> signal_values;
        VkTimelineSemaphoreSubmitInfo timeline_info{};
        bool has_timeline = false;
    };

    std::vector<VkSubmitInfo> remote_submits;
    std::vector<SubmitStorage> storage;
    if (submitCount > 0) {
        remote_submits.resize(submitCount);
        storage.resize(submitCount);
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo& src = pSubmits[i];
        VkSubmitInfo& dst = remote_submits[i];
        SubmitStorage& slot = storage[i];
        dst = src;

        if (src.waitSemaphoreCount > 0) {
            if (!src.pWaitSemaphores || !src.pWaitDstStageMask) {
                std::cerr << "[Client ICD] vkQueueSubmit: invalid wait semaphore arrays\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.wait_local.assign(src.pWaitSemaphores, src.pWaitSemaphores + src.waitSemaphoreCount);
            slot.wait_remote.resize(src.waitSemaphoreCount);
            slot.wait_stages.assign(src.pWaitDstStageMask, src.pWaitDstStageMask + src.waitSemaphoreCount);
            for (uint32_t j = 0; j < src.waitSemaphoreCount; ++j) {
                VkSemaphore wait_sem = src.pWaitSemaphores[j];
                if (!g_sync_state.has_semaphore(wait_sem)) {
                    std::cerr << "[Client ICD] vkQueueSubmit: wait semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.wait_remote[j] = g_sync_state.get_remote_semaphore(wait_sem);
                if (slot.wait_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pWaitSemaphores = slot.wait_remote.data();
            dst.pWaitDstStageMask = slot.wait_stages.data();
        } else {
            dst.pWaitSemaphores = nullptr;
            dst.pWaitDstStageMask = nullptr;
        }

        if (src.commandBufferCount > 0) {
            if (!src.pCommandBuffers) {
                std::cerr << "[Client ICD] vkQueueSubmit: command buffers missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.remote_cbs.resize(src.commandBufferCount);
            for (uint32_t j = 0; j < src.commandBufferCount; ++j) {
                VkCommandBuffer local_cb = src.pCommandBuffers[j];
                if (!g_command_buffer_state.has_command_buffer(local_cb)) {
                    std::cerr << "[Client ICD] vkQueueSubmit: command buffer not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (g_command_buffer_state.get_buffer_state(local_cb) != CommandBufferLifecycleState::EXECUTABLE) {
                    std::cerr << "[Client ICD] vkQueueSubmit: command buffer not executable\n";
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }
                VkCommandBuffer remote_cb = get_remote_command_buffer_handle(local_cb);
                if (remote_cb == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.remote_cbs[j] = remote_cb;
            }
            dst.pCommandBuffers = slot.remote_cbs.data();
        } else {
            dst.pCommandBuffers = nullptr;
        }

        if (src.signalSemaphoreCount > 0) {
            if (!src.pSignalSemaphores) {
                std::cerr << "[Client ICD] vkQueueSubmit: signal semaphores missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.signal_local.assign(src.pSignalSemaphores, src.pSignalSemaphores + src.signalSemaphoreCount);
            slot.signal_remote.resize(src.signalSemaphoreCount);
            for (uint32_t j = 0; j < src.signalSemaphoreCount; ++j) {
                VkSemaphore signal_sem = src.pSignalSemaphores[j];
                if (!g_sync_state.has_semaphore(signal_sem)) {
                    std::cerr << "[Client ICD] vkQueueSubmit: signal semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.signal_remote[j] = g_sync_state.get_remote_semaphore(signal_sem);
                if (slot.signal_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pSignalSemaphores = slot.signal_remote.data();
        } else {
            dst.pSignalSemaphores = nullptr;
        }

        const VkTimelineSemaphoreSubmitInfo* timeline = find_timeline_submit_info(src.pNext);
        if (timeline) {
            slot.timeline_info = *timeline;
            if (timeline->waitSemaphoreValueCount) {
                slot.wait_values.assign(timeline->pWaitSemaphoreValues,
                                        timeline->pWaitSemaphoreValues + timeline->waitSemaphoreValueCount);
                slot.timeline_info.pWaitSemaphoreValues = slot.wait_values.data();
            }
            if (timeline->signalSemaphoreValueCount) {
                slot.signal_values.assign(timeline->pSignalSemaphoreValues,
                                          timeline->pSignalSemaphoreValues + timeline->signalSemaphoreValueCount);
                slot.timeline_info.pSignalSemaphoreValues = slot.signal_values.data();
            }
            dst.pNext = &slot.timeline_info;
            slot.has_timeline = true;
        } else {
            dst.pNext = nullptr;
            slot.has_timeline = false;
        }
    }

    const VkSubmitInfo* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    VkResult result = vn_call_vkQueueSubmit(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkQueueSubmit failed: " << result << "\n";
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    for (uint32_t i = 0; i < submitCount; ++i) {
        const SubmitStorage& slot = storage[i];
        for (VkSemaphore wait_sem : slot.wait_local) {
            if (g_sync_state.get_semaphore_type(wait_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(wait_sem, false);
            }
        }
        if (slot.has_timeline && !slot.wait_values.empty()) {
            for (size_t j = 0; j < slot.wait_local.size() && j < slot.wait_values.size(); ++j) {
                if (g_sync_state.get_semaphore_type(slot.wait_local[j]) == VK_SEMAPHORE_TYPE_TIMELINE) {
                    g_sync_state.set_timeline_value(slot.wait_local[j], slot.wait_values[j]);
                }
            }
        }
        for (size_t j = 0; j < slot.signal_local.size(); ++j) {
            VkSemaphore signal_sem = slot.signal_local[j];
            if (g_sync_state.get_semaphore_type(signal_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(signal_sem, true);
            } else if (slot.has_timeline && j < slot.signal_values.size()) {
                g_sync_state.set_timeline_value(signal_sem, slot.signal_values[j]);
            }
        }
    }

    std::cout << "[Client ICD] vkQueueSubmit completed\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    std::cout << "[Client ICD] vkQueueWaitIdle called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = vn_call_vkQueueWaitIdle(&g_ring, remote_queue);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkQueueWaitIdle failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    std::cout << "[Client ICD] vkDeviceWaitIdle called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        std::cerr << "[Client ICD] Unknown device in vkDeviceWaitIdle\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkDeviceWaitIdle(&g_ring, icd_device->remote_handle);
    if (result != VK_SUCCESS) {
        std::cerr << "[Client ICD] vkDeviceWaitIdle failed: " << result << "\n";
    }
    return result;
}

} // extern "C"
