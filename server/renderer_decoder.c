#define VN_RENDERER_STATIC_DISPATCH 1

#include "renderer_decoder.h"

#include <stdlib.h>
#include <string.h>

#include "server_state_bridge.h"
#include "branding.h"
#include "vn_protocol_renderer.h"
#include "vn_cs.h"
#include "utils/logging_c.h"

struct VenusRenderer {
    struct vn_dispatch_context ctx;
    struct vn_cs_decoder* decoder;
    struct vn_cs_encoder* encoder;
    struct ServerState* state;
};

static bool command_buffer_recording_guard(struct ServerState* state,
                                           VkCommandBuffer command_buffer,
                                           const char* name) {
    if (!server_state_bridge_command_buffer_is_recording(state, command_buffer)) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s requires command buffer in RECORDING state", name);
        server_state_bridge_mark_command_buffer_invalid(state, command_buffer);
        return false;
    }
    return true;
}

static VkCommandBuffer get_real_command_buffer(struct ServerState* state,
                                               VkCommandBuffer command_buffer,
                                               const char* name) {
    VkCommandBuffer real = server_state_bridge_get_real_command_buffer(state, command_buffer);
    if (real == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to translate command buffer for %s", name);
        server_state_bridge_mark_command_buffer_invalid(state, command_buffer);
    }
    return real;
}

static VkBuffer get_real_buffer(struct ServerState* state, VkBuffer buffer, const char* name) {
    VkBuffer real = server_state_bridge_get_real_buffer(state, buffer);
    if (real == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to translate buffer for %s", name);
    }
    return real;
}

static VkImage get_real_image(struct ServerState* state, VkImage image, const char* name) {
    VkImage real = server_state_bridge_get_real_image(state, image);
    if (real == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to translate image for %s", name);
    }
    return real;
}

static void server_dispatch_vkCreateInstance(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCreateInstance* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateInstance");
    args->ret = VK_SUCCESS;
    if (!args->pInstance) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pInstance is NULL");
        return;
    }
    *args->pInstance = server_state_bridge_alloc_instance((struct ServerState*)ctx->data);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created instance handle: %p", (void*)*args->pInstance);
}

static void server_dispatch_vkDestroyInstance(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkDestroyInstance* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyInstance (handle: %p)", (void*)args->instance);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (server_state_bridge_instance_exists(state, args->instance)) {
        server_state_bridge_remove_instance(state, args->instance);
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Instance destroyed");
    } else {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Instance not found");
    }
}

static void server_dispatch_vkEnumerateInstanceVersion(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkEnumerateInstanceVersion* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumerateInstanceVersion");
    (void)ctx;
    args->ret = VK_SUCCESS;
    if (args->pApiVersion) {
        *args->pApiVersion = VK_API_VERSION_1_3;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning API version: 1.3");
    }
}

static void server_dispatch_vkEnumerateInstanceExtensionProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumerateInstanceExtensionProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumerateInstanceExtensionProperties");
    (void)ctx;
    args->ret = VK_SUCCESS;

    if (!args->pPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPropertyCount is NULL");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkEnumerateInstanceExtensionProperties(
        args->pLayerName, args->pPropertyCount, args->pProperties);
    if (args->ret == VK_SUCCESS || args->ret == VK_INCOMPLETE) {
        VP_LOG_INFO(SERVER,
                    "[Venus Server]   -> Returned %u instance extensions%s",
                    args->pPropertyCount ? *args->pPropertyCount : 0,
                    args->ret == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "");
    } else if (args->ret == VK_ERROR_LAYER_NOT_PRESENT) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Requested layer not present");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkEnumerateInstanceExtensionProperties failed: %d", args->ret);
    }
}

static void server_dispatch_vkEnumerateInstanceLayerProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumerateInstanceLayerProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumerateInstanceLayerProperties");
    (void)ctx;
    args->ret = VK_SUCCESS;

    if (!args->pPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPropertyCount is NULL");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkEnumerateInstanceLayerProperties(args->pPropertyCount, args->pProperties);
    if (args->ret == VK_SUCCESS || args->ret == VK_INCOMPLETE) {
        VP_LOG_INFO(SERVER,
                    "[Venus Server]   -> Returned %u instance layers%s",
                    args->pPropertyCount ? *args->pPropertyCount : 0,
                    args->ret == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkEnumerateInstanceLayerProperties failed: %d", args->ret);
    }
}

static void server_dispatch_vkEnumeratePhysicalDevices(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkEnumeratePhysicalDevices* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumeratePhysicalDevices (instance: %p)", (void*)args->instance);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pPhysicalDeviceCount) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPhysicalDeviceCount is NULL");
        return;
    }

    const uint32_t available_devices = 1;
    if (!args->pPhysicalDevices) {
        *args->pPhysicalDeviceCount = available_devices;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning device count: %u", available_devices);
        return;
    }

    const uint32_t max_out = *args->pPhysicalDeviceCount;
    const uint32_t to_write = available_devices < max_out ? available_devices : max_out;
    for (uint32_t i = 0; i < to_write; ++i) {
        args->pPhysicalDevices[i] = server_state_bridge_get_fake_device(state);
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Device %u: %p", i, (void*)args->pPhysicalDevices[i]);
    }
    *args->pPhysicalDeviceCount = to_write;

    if (max_out < available_devices) {
        args->ret = VK_INCOMPLETE;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning VK_INCOMPLETE");
    }
}

// Phase 3: Physical device query handlers
static void server_dispatch_vkGetPhysicalDeviceProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pProperties is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceProperties(real_device, args->pProperties);
    vp_branding_apply_properties(args->pProperties);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned branded properties");
}

static void server_dispatch_vkGetPhysicalDeviceFeatures(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFeatures* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceFeatures");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pFeatures) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pFeatures is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceFeatures(real_device, args->pFeatures);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned real features");
}

static void server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceQueueFamilyProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceQueueFamilyProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pQueueFamilyPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pQueueFamilyPropertyCount is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(real_device,
                                             args->pQueueFamilyPropertyCount,
                                             args->pQueueFamilyProperties);
    if (args->pQueueFamilyProperties) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned %u queue families", *args->pQueueFamilyPropertyCount);
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned count: %u", *args->pQueueFamilyPropertyCount);
    }
}

static void server_dispatch_vkGetPhysicalDeviceMemoryProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceMemoryProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceMemoryProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pMemoryProperties is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceMemoryProperties(real_device, args->pMemoryProperties);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned real memory properties");
}

static void server_dispatch_vkGetPhysicalDeviceFormatProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFormatProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceFormatProperties (format: %d)", args->format);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pFormatProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pFormatProperties is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceFormatProperties(real_device, args->format, args->pFormatProperties);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned real format properties");
}

static void server_dispatch_vkGetPhysicalDeviceImageFormatProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceImageFormatProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceImageFormatProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pImageFormatProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pImageFormatProperties is NULL");
        args->ret = VK_ERROR_FORMAT_NOT_SUPPORTED;
        return;
    }

    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkGetPhysicalDeviceImageFormatProperties(real_device,
                                                         args->format,
                                                         args->type,
                                                         args->tiling,
                                                         args->usage,
                                                         args->flags,
                                                         args->pImageFormatProperties);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> vkGetPhysicalDeviceImageFormatProperties returned %d", args->ret);
    }
}

static void server_dispatch_vkGetPhysicalDeviceImageFormatProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceImageFormatProperties2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceImageFormatProperties2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pImageFormatInfo || !args->pImageFormatProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pImageFormatInfo/pImageFormatProperties is NULL");
        args->ret = VK_ERROR_FORMAT_NOT_SUPPORTED;
        return;
    }

    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkGetPhysicalDeviceImageFormatProperties2(real_device,
                                                          args->pImageFormatInfo,
                                                          args->pImageFormatProperties);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> vkGetPhysicalDeviceImageFormatProperties2 returned %d", args->ret);
    }
}

static void server_dispatch_vkGetPhysicalDeviceProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceProperties2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceProperties2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pProperties is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceProperties2(real_device, args->pProperties);
    vp_branding_apply_properties2(args->pProperties);
}

static void server_dispatch_vkGetPhysicalDeviceFeatures2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFeatures2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceFeatures2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pFeatures) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pFeatures is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceFeatures2(real_device, args->pFeatures);
}

static void server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceQueueFamilyProperties2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceQueueFamilyProperties2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pQueueFamilyPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pQueueFamilyPropertyCount is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(real_device,
                                              args->pQueueFamilyPropertyCount,
                                              args->pQueueFamilyProperties);
}

static void server_dispatch_vkGetPhysicalDeviceMemoryProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceMemoryProperties2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceMemoryProperties2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryProperties) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pMemoryProperties is NULL");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }
    vkGetPhysicalDeviceMemoryProperties2(real_device, args->pMemoryProperties);
}

static void server_dispatch_vkEnumerateDeviceExtensionProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumerateDeviceExtensionProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumerateDeviceExtensionProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPropertyCount is NULL");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    if (args->pLayerName) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Layer request unsupported: %s", args->pLayerName);
        args->ret = VK_ERROR_LAYER_NOT_PRESENT;
        return;
    }

    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkEnumerateDeviceExtensionProperties(
        real_device, args->pLayerName, args->pPropertyCount, args->pProperties);
    if (args->ret == VK_SUCCESS || args->ret == VK_INCOMPLETE) {
        VP_LOG_INFO(SERVER,
                    "[Venus Server]   -> Returned %u extensions%s",
                    args->pPropertyCount ? *args->pPropertyCount : 0,
                    args->ret == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkEnumerateDeviceExtensionProperties failed: %d", args->ret);
    }
}

static void server_dispatch_vkEnumerateDeviceLayerProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumerateDeviceLayerProperties* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumerateDeviceLayerProperties");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pPropertyCount) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPropertyCount is NULL");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    args->ret = vkEnumerateDeviceLayerProperties(
        real_device, args->pPropertyCount, args->pProperties);
    if (args->ret == VK_SUCCESS || args->ret == VK_INCOMPLETE) {
        VP_LOG_INFO(SERVER,
                    "[Venus Server]   -> Returned %u layers%s",
                    args->pPropertyCount ? *args->pPropertyCount : 0,
                    args->ret == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkEnumerateDeviceLayerProperties failed: %d", args->ret);
    }
}

// Phase 3: Device creation/destruction handlers
static void server_dispatch_vkCreateDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDevice* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateDevice (physical device: %p)", (void*)args->physicalDevice);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pDevice || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pDevice or pCreateInfo is NULL");
        return;
    }

    VkPhysicalDevice real_physical =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_physical == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown physical device");
        return;
    }

    VkDevice real_device = VK_NULL_HANDLE;
    VkResult create_result = vkCreateDevice(real_physical, args->pCreateInfo, args->pAllocator, &real_device);
    if (create_result != VK_SUCCESS) {
        args->ret = create_result;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCreateDevice failed: %d", create_result);
        return;
    }

    VkDevice client_handle =
        server_state_bridge_alloc_device(state, args->physicalDevice, real_device);
    if (client_handle == VK_NULL_HANDLE) {
        vkDestroyDevice(real_device, args->pAllocator);
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to allocate server device handle");
        return;
    }

    *args->pDevice = client_handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created device handle: %p", (void*)*args->pDevice);
}

static void server_dispatch_vkDestroyDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDevice* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyDevice (handle: %p)", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->device != VK_NULL_HANDLE && server_state_bridge_device_exists(state, args->device)) {
        VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
        if (real_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(real_device);
            vkDestroyDevice(real_device, args->pAllocator);
        }
        server_state_bridge_remove_device(state, args->device);
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Device destroyed");
    } else {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Device not found or NULL");
    }
}

static void server_dispatch_vkGetDeviceQueue(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceQueue* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceQueue (device: %p, family: %u, index: %u)",
           (void*)args->device, args->queueFamilyIndex, args->queueIndex);
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pQueue) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pQueue is NULL");
        return;
    }

    // Check if queue already exists
    VkQueue existing = server_state_bridge_find_queue(state, args->device, args->queueFamilyIndex, args->queueIndex);
    if (existing != VK_NULL_HANDLE) {
        *args->pQueue = existing;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned existing queue: %p", (void*)existing);
    } else {
        VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
        if (real_device == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
            return;
        }
        VkQueue real_queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(real_device, args->queueFamilyIndex, args->queueIndex, &real_queue);
        if (real_queue == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkGetDeviceQueue failed");
            return;
        }
        *args->pQueue = server_state_bridge_alloc_queue(
            state, args->device, args->queueFamilyIndex, args->queueIndex, real_queue);
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Created new queue: %p", (void*)*args->pQueue);
    }
}

// Phase 4: Resource and memory management
static void server_dispatch_vkAllocateMemory(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkAllocateMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkAllocateMemory");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pMemory || !args->pAllocateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pMemory or pAllocateInfo is NULL");
        return;
    }

    VkDeviceMemory handle = server_state_bridge_alloc_memory(state, args->device, args->pAllocateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to allocate memory");
        return;
    }

    *args->pMemory = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Allocated memory handle: %p (size=%llu)",
           (void*)handle, (unsigned long long)args->pAllocateInfo->allocationSize);
}

static void server_dispatch_vkFreeMemory(struct vn_dispatch_context* ctx,
                                         struct vn_command_vkFreeMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkFreeMemory (memory: %p)", (void*)args->memory);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->memory == VK_NULL_HANDLE) {
        return;
    }

    if (!server_state_bridge_free_memory(state, args->memory)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Memory handle not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Memory freed");
    }
}

static void server_dispatch_vkCreateBuffer(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCreateBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateBuffer (device: %p)", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pBuffer || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pBuffer or pCreateInfo is NULL");
        return;
    }

    VkBuffer handle = server_state_bridge_create_buffer(state, args->device, args->pCreateInfo);
    *args->pBuffer = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created buffer handle: %p (size=%llu)",
           (void*)handle, (unsigned long long)args->pCreateInfo->size);
}

static void server_dispatch_vkDestroyBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkDestroyBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyBuffer (buffer: %p)", (void*)args->buffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_buffer(state, args->buffer)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Buffer not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Buffer destroyed");
    }
}

static void server_dispatch_vkGetBufferMemoryRequirements(struct vn_dispatch_context* ctx,
                                                          struct vn_command_vkGetBufferMemoryRequirements* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetBufferMemoryRequirements");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pMemoryRequirements is NULL");
        return;
    }

    if (!server_state_bridge_get_buffer_memory_requirements(state, args->buffer, args->pMemoryRequirements)) {
        memset(args->pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Buffer not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Requirements: size=%llu alignment=%llu",
               (unsigned long long)args->pMemoryRequirements->size,
               (unsigned long long)args->pMemoryRequirements->alignment);
    }
}

static void server_dispatch_vkBindBufferMemory(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkBindBufferMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkBindBufferMemory (buffer: %p)", (void*)args->buffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_buffer_memory(state, args->buffer, args->memory, args->memoryOffset);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Buffer bound (memory=%p, offset=%llu)",
               (void*)args->memory, (unsigned long long)args->memoryOffset);
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to bind buffer (result=%d)", args->ret);
    }
}

static void server_dispatch_vkCreateImage(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCreateImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateImage (device: %p)", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pImage || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pImage or pCreateInfo is NULL");
        return;
    }

    VkImage handle = server_state_bridge_create_image(state, args->device, args->pCreateInfo);
    *args->pImage = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created image handle: %p (format=%d)",
           (void*)handle, args->pCreateInfo->format);
}

static void server_dispatch_vkDestroyImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkDestroyImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyImage (image: %p)", (void*)args->image);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_image(state, args->image)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Image not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Image destroyed");
    }
}

static void server_dispatch_vkCreateImageView(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCreateImageView* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateImageView");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pView || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pView or pCreateInfo is NULL");
        return;
    }

    VkImageView handle = server_state_bridge_create_image_view(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create image view");
        return;
    }

    *args->pView = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created image view handle: %p", (void*)handle);
}

static void server_dispatch_vkDestroyImageView(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkDestroyImageView* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyImageView (view: %p)", (void*)args->imageView);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->imageView) {
        return;
    }
    if (!server_state_bridge_destroy_image_view(state, args->imageView)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Image view not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Image view destroyed");
    }
}

static void server_dispatch_vkCreateBufferView(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCreateBufferView* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateBufferView");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pView || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pView or pCreateInfo is NULL");
        return;
    }

    VkBufferView handle = server_state_bridge_create_buffer_view(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create buffer view");
        return;
    }

    *args->pView = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created buffer view handle: %p", (void*)handle);
}

static void server_dispatch_vkDestroyBufferView(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkDestroyBufferView* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyBufferView (view: %p)", (void*)args->bufferView);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->bufferView) {
        return;
    }
    if (!server_state_bridge_destroy_buffer_view(state, args->bufferView)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Buffer view not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Buffer view destroyed");
    }
}

static void server_dispatch_vkCreateSampler(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCreateSampler* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateSampler");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pSampler || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pSampler or pCreateInfo is NULL");
        return;
    }

    VkSampler handle = server_state_bridge_create_sampler(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create sampler");
        return;
    }

    *args->pSampler = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created sampler handle: %p", (void*)handle);
}

static void server_dispatch_vkDestroySampler(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkDestroySampler* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroySampler (sampler: %p)", (void*)args->sampler);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->sampler) {
        return;
    }
    if (!server_state_bridge_destroy_sampler(state, args->sampler)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Sampler not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Sampler destroyed");
    }
}

static void server_dispatch_vkCreateShaderModule(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCreateShaderModule* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateShaderModule");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pShaderModule) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkShaderModule handle =
        server_state_bridge_create_shader_module(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create shader module");
        return;
    }

    *args->pShaderModule = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Shader module created: %p", (void*)handle);
}

static void server_dispatch_vkDestroyShaderModule(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkDestroyShaderModule* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyShaderModule (module: %p)",
           (void*)args->shaderModule);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->shaderModule != VK_NULL_HANDLE) {
        server_state_bridge_destroy_shader_module(state, args->shaderModule);
    }
}

static void server_dispatch_vkCreateDescriptorSetLayout(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDescriptorSetLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateDescriptorSetLayout");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pSetLayout) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkDescriptorSetLayout layout =
        server_state_bridge_create_descriptor_set_layout(state, args->device, args->pCreateInfo);
    if (layout == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create descriptor set layout");
        return;
    }
    *args->pSetLayout = layout;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Descriptor set layout created: %p", (void*)layout);
}

static void server_dispatch_vkDestroyDescriptorSetLayout(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDescriptorSetLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyDescriptorSetLayout (layout: %p)",
           (void*)args->descriptorSetLayout);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->descriptorSetLayout != VK_NULL_HANDLE) {
        server_state_bridge_destroy_descriptor_set_layout(state, args->descriptorSetLayout);
    }
}

static void server_dispatch_vkCreateDescriptorPool(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCreateDescriptorPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateDescriptorPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pDescriptorPool) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkDescriptorPool pool =
        server_state_bridge_create_descriptor_pool(state, args->device, args->pCreateInfo);
    if (pool == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create descriptor pool");
        return;
    }
    *args->pDescriptorPool = pool;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Descriptor pool created: %p", (void*)pool);
}

static void server_dispatch_vkDestroyDescriptorPool(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkDestroyDescriptorPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyDescriptorPool (pool: %p)",
           (void*)args->descriptorPool);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->descriptorPool != VK_NULL_HANDLE) {
        server_state_bridge_destroy_descriptor_pool(state, args->descriptorPool);
    }
}

static void server_dispatch_vkResetDescriptorPool(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkResetDescriptorPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetDescriptorPool (pool: %p)",
           (void*)args->descriptorPool);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_descriptor_pool(state,
                                                          args->descriptorPool,
                                                          args->flags);
}

static void server_dispatch_vkAllocateDescriptorSets(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkAllocateDescriptorSets* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkAllocateDescriptorSets (count=%u)",
           args->pAllocateInfo ? args->pAllocateInfo->descriptorSetCount : 0);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pAllocateInfo || !args->pDescriptorSets) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing allocate info or output pointer");
        return;
    }

    args->ret = server_state_bridge_allocate_descriptor_sets(state,
                                                             args->device,
                                                             args->pAllocateInfo,
                                                             args->pDescriptorSets);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Descriptor sets allocated");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Allocation failed (%d)", args->ret);
    }
}

static void server_dispatch_vkFreeDescriptorSets(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkFreeDescriptorSets* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkFreeDescriptorSets (count=%u)",
           args->descriptorSetCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_free_descriptor_sets(state,
                                                         args->device,
                                                         args->descriptorPool,
                                                         args->descriptorSetCount,
                                                         args->pDescriptorSets);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Free descriptor sets failed (%d)", args->ret);
    }
}

static VkDescriptorType descriptor_type_from_write(const VkWriteDescriptorSet* write) {
    return write ? write->descriptorType : VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

static bool write_uses_buffer(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return true;
    default:
        return false;
    }
}

static void server_dispatch_vkUpdateDescriptorSets(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkUpdateDescriptorSets* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkUpdateDescriptorSets (writes=%u, copies=%u)",
           args->descriptorWriteCount,
           args->descriptorCopyCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        return;
    }

    VkWriteDescriptorSet* writes = NULL;
    VkDescriptorBufferInfo** buffer_arrays = NULL;
    VkCopyDescriptorSet* copies = NULL;
    VkResult result = VK_SUCCESS;

    if (args->descriptorWriteCount > 0) {
        writes = calloc(args->descriptorWriteCount, sizeof(*writes));
        buffer_arrays = calloc(args->descriptorWriteCount, sizeof(*buffer_arrays));
        if (!writes || !buffer_arrays) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for descriptor writes");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup;
        }
    }

    for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet* src = &args->pDescriptorWrites[i];
        writes[i] = *src;
        writes[i].dstSet = server_state_bridge_get_real_descriptor_set(state, src->dstSet);
        if (writes[i].dstSet == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown descriptor set in write %u", i);
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto cleanup;
        }

        if (write_uses_buffer(src->descriptorType)) {
            if (!src->pBufferInfo) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing buffer info in write %u", i);
                result = VK_ERROR_INITIALIZATION_FAILED;
                goto cleanup;
            }
            buffer_arrays[i] =
                calloc(src->descriptorCount ? src->descriptorCount : 1, sizeof(VkDescriptorBufferInfo));
            if (!buffer_arrays[i]) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for buffer infos");
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->descriptorCount; ++j) {
                buffer_arrays[i][j] = src->pBufferInfo[j];
                buffer_arrays[i][j].buffer =
                    server_state_bridge_get_real_buffer(state, src->pBufferInfo[j].buffer);
                if (buffer_arrays[i][j].buffer == VK_NULL_HANDLE) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown buffer in write %u", i);
                    result = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
            }
            writes[i].pBufferInfo = buffer_arrays[i];
            writes[i].pImageInfo = NULL;
            writes[i].pTexelBufferView = NULL;
        }
    }

    if (args->descriptorCopyCount > 0) {
        copies = calloc(args->descriptorCopyCount, sizeof(*copies));
        if (!copies) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for descriptor copies");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup;
        }
    }

    for (uint32_t i = 0; i < args->descriptorCopyCount; ++i) {
        copies[i] = args->pDescriptorCopies[i];
        copies[i].srcSet =
            server_state_bridge_get_real_descriptor_set(state, args->pDescriptorCopies[i].srcSet);
        copies[i].dstSet =
            server_state_bridge_get_real_descriptor_set(state, args->pDescriptorCopies[i].dstSet);
        if (copies[i].srcSet == VK_NULL_HANDLE || copies[i].dstSet == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown descriptor set in copy %u", i);
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto cleanup;
        }
    }

    vkUpdateDescriptorSets(real_device,
                           args->descriptorWriteCount,
                           writes,
                           args->descriptorCopyCount,
                           copies);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Descriptor sets updated");

cleanup:
    if (buffer_arrays) {
        for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
            free(buffer_arrays[i]);
        }
        free(buffer_arrays);
    }
    free(writes);
    free(copies);
    (void)result;
}

static void server_dispatch_vkCreatePipelineLayout(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCreatePipelineLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreatePipelineLayout");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pPipelineLayout) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkPipelineLayout layout =
        server_state_bridge_create_pipeline_layout(state, args->device, args->pCreateInfo);
    if (layout == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create pipeline layout");
        return;
    }
    *args->pPipelineLayout = layout;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Pipeline layout created: %p", (void*)layout);
}

static void server_dispatch_vkDestroyPipelineLayout(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkDestroyPipelineLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyPipelineLayout (layout: %p)",
           (void*)args->pipelineLayout);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->pipelineLayout != VK_NULL_HANDLE) {
        server_state_bridge_destroy_pipeline_layout(state, args->pipelineLayout);
    }
}

static void server_dispatch_vkCreateRenderPass(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCreateRenderPass* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateRenderPass");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pRenderPass) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkRenderPass render_pass =
        server_state_bridge_create_render_pass(state, args->device, args->pCreateInfo);
    if (render_pass == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create render pass");
        return;
    }

    *args->pRenderPass = render_pass;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Render pass created: %p", (void*)render_pass);
}

static void server_dispatch_vkCreateRenderPass2(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCreateRenderPass2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateRenderPass2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pRenderPass) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkRenderPass render_pass =
        server_state_bridge_create_render_pass2(state, args->device, args->pCreateInfo);
    if (render_pass == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create render pass2");
        return;
    }

    *args->pRenderPass = render_pass;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Render pass (v2) created: %p", (void*)render_pass);
}

static void server_dispatch_vkDestroyRenderPass(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkDestroyRenderPass* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyRenderPass (handle: %p)",
           (void*)args->renderPass);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->renderPass != VK_NULL_HANDLE) {
        server_state_bridge_destroy_render_pass(state, args->renderPass);
    }
}

static void server_dispatch_vkCreateFramebuffer(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCreateFramebuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateFramebuffer");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pFramebuffer) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkFramebuffer framebuffer =
        server_state_bridge_create_framebuffer(state, args->device, args->pCreateInfo);
    if (framebuffer == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create framebuffer");
        return;
    }

    *args->pFramebuffer = framebuffer;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Framebuffer created: %p", (void*)framebuffer);
}

static void server_dispatch_vkDestroyFramebuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkDestroyFramebuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyFramebuffer (handle: %p)",
           (void*)args->framebuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->framebuffer != VK_NULL_HANDLE) {
        server_state_bridge_destroy_framebuffer(state, args->framebuffer);
    }
}

static void server_dispatch_vkCreateComputePipelines(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCreateComputePipelines* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateComputePipelines (count=%u)",
           args->createInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfos || !args->pPipelines) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create infos or output array");
        return;
    }

    args->ret = server_state_bridge_create_compute_pipelines(state,
                                                             args->device,
                                                             args->pipelineCache,
                                                             args->createInfoCount,
                                                             args->pCreateInfos,
                                                             args->pPipelines);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Compute pipeline(s) created");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Compute pipeline creation failed (%d)", args->ret);
    }
}

static void server_dispatch_vkCreateGraphicsPipelines(struct vn_dispatch_context* ctx,
                                                      struct vn_command_vkCreateGraphicsPipelines* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateGraphicsPipelines (count=%u)",
           args->createInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfos || !args->pPipelines) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create infos or output array");
        return;
    }

    args->ret = server_state_bridge_create_graphics_pipelines(state,
                                                              args->device,
                                                              args->pipelineCache,
                                                              args->createInfoCount,
                                                              args->pCreateInfos,
                                                              args->pPipelines);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Graphics pipeline(s) created");
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Graphics pipeline creation failed (%d)", args->ret);
    }
}

static void server_dispatch_vkDestroyPipeline(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkDestroyPipeline* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyPipeline (pipeline: %p)",
           (void*)args->pipeline);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->pipeline != VK_NULL_HANDLE) {
        server_state_bridge_destroy_pipeline(state, args->pipeline);
    }
}



static void server_dispatch_vkGetImageMemoryRequirements(struct vn_dispatch_context* ctx,
                                                         struct vn_command_vkGetImageMemoryRequirements* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetImageMemoryRequirements");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pMemoryRequirements is NULL");
        return;
    }

    if (!server_state_bridge_get_image_memory_requirements(state, args->image, args->pMemoryRequirements)) {
        memset(args->pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Image not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Requirements: size=%llu alignment=%llu",
               (unsigned long long)args->pMemoryRequirements->size,
               (unsigned long long)args->pMemoryRequirements->alignment);
    }
}

static void server_dispatch_vkBindImageMemory(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkBindImageMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkBindImageMemory (image: %p)", (void*)args->image);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_image_memory(state, args->image, args->memory, args->memoryOffset);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Image bound (memory=%p, offset=%llu)",
               (void*)args->memory, (unsigned long long)args->memoryOffset);
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to bind image (result=%d)", args->ret);
    }
}

static void server_dispatch_vkGetImageSubresourceLayout(struct vn_dispatch_context* ctx,
                                                        struct vn_command_vkGetImageSubresourceLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetImageSubresourceLayout");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pLayout || !args->pSubresource) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pLayout or pSubresource is NULL");
        return;
    }

    if (!server_state_bridge_get_image_subresource_layout(state, args->image, args->pSubresource, args->pLayout)) {
        memset(args->pLayout, 0, sizeof(VkSubresourceLayout));
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Image not found or invalid subresource");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned subresource layout (offset=%llu)",
               (unsigned long long)args->pLayout->offset);
    }
}

static void server_dispatch_vkCreateCommandPool(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCreateCommandPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateCommandPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pCommandPool) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters");
        return;
    }

    VkCommandPool handle = server_state_bridge_create_command_pool(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to allocate command pool");
        return;
    }
    *args->pCommandPool = handle;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created command pool: %p", (void*)handle);
}

static void server_dispatch_vkDestroyCommandPool(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkDestroyCommandPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyCommandPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_command_pool(state, args->commandPool)) {
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Command pool not found");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command pool destroyed");
    }
}

static void server_dispatch_vkResetCommandPool(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkResetCommandPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetCommandPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_command_pool(state, args->commandPool, args->flags);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command pool reset");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to reset command pool (result=%d)", args->ret);
    }
}

static void server_dispatch_vkAllocateCommandBuffers(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkAllocateCommandBuffers* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkAllocateCommandBuffers (count=%u)", args->pAllocateInfo ? args->pAllocateInfo->commandBufferCount : 0);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_allocate_command_buffers(state, args->device, args->pAllocateInfo, args->pCommandBuffers);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command buffers allocated");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Allocation failed (result=%d)", args->ret);
    }
}

static void server_dispatch_vkFreeCommandBuffers(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkFreeCommandBuffers* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkFreeCommandBuffers (count=%u)", args->commandBufferCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_free_command_buffers(state, args->commandPool, args->commandBufferCount, args->pCommandBuffers);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Command buffers freed");
}

static void server_dispatch_vkBeginCommandBuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkBeginCommandBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkBeginCommandBuffer (%p)", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_begin_command_buffer(state, args->commandBuffer, args->pBeginInfo);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command buffer recording started");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to begin command buffer (result=%d)", args->ret);
    }
}

static void server_dispatch_vkEndCommandBuffer(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkEndCommandBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEndCommandBuffer (%p)", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_end_command_buffer(state, args->commandBuffer);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command buffer ended");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to end command buffer (result=%d)", args->ret);
    }
}

static void server_dispatch_vkResetCommandBuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkResetCommandBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetCommandBuffer (%p)", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_command_buffer(state, args->commandBuffer, args->flags);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Command buffer reset");
    } else {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Failed to reset command buffer (result=%d)", args->ret);
    }
}

static void server_dispatch_vkCmdCopyBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdCopyBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyBuffer (%u regions)", args->regionCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyBuffer")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_copy_buffer(state,
                                                      args->srcBuffer,
                                                      args->dstBuffer,
                                                      args->regionCount,
                                                      args->pRegions)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyBuffer");
    VkBuffer real_src = get_real_buffer(state, args->srcBuffer, "vkCmdCopyBuffer");
    VkBuffer real_dst = get_real_buffer(state, args->dstBuffer, "vkCmdCopyBuffer");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdCopyBuffer(real_cb, real_src, real_dst, args->regionCount, args->pRegions);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyBuffer recorded");
}

static void server_dispatch_vkCmdCopyImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCmdCopyImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyImage (%u regions)", args->regionCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyImage")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_copy_image(state,
                                                     args->srcImage,
                                                     args->dstImage,
                                                     args->regionCount,
                                                     args->pRegions)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyImage");
    VkImage real_src = get_real_image(state, args->srcImage, "vkCmdCopyImage");
    VkImage real_dst = get_real_image(state, args->dstImage, "vkCmdCopyImage");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdCopyImage(real_cb,
                   real_src,
                   args->srcImageLayout,
                   real_dst,
                   args->dstImageLayout,
                   args->regionCount,
                   args->pRegions);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyImage recorded");
}

static void server_dispatch_vkCmdBlitImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCmdBlitImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBlitImage (%u regions)", args->regionCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBlitImage")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_blit_image(state,
                                                     args->srcImage,
                                                     args->dstImage,
                                                     args->regionCount,
                                                     args->pRegions)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBlitImage");
    VkImage real_src = get_real_image(state, args->srcImage, "vkCmdBlitImage");
    VkImage real_dst = get_real_image(state, args->dstImage, "vkCmdBlitImage");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBlitImage(real_cb,
                   real_src,
                   args->srcImageLayout,
                   real_dst,
                   args->dstImageLayout,
                   args->regionCount,
                   args->pRegions,
                   args->filter);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBlitImage recorded");
}

static void server_dispatch_vkCmdCopyBufferToImage(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdCopyBufferToImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyBufferToImage (%u regions)", args->regionCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyBufferToImage")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_copy_buffer_to_image(state,
                                                               args->srcBuffer,
                                                               args->dstImage,
                                                               args->regionCount,
                                                               args->pRegions)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyBufferToImage");
    VkBuffer real_src = get_real_buffer(state, args->srcBuffer, "vkCmdCopyBufferToImage");
    VkImage real_dst = get_real_image(state, args->dstImage, "vkCmdCopyBufferToImage");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdCopyBufferToImage(real_cb,
                           real_src,
                           real_dst,
                           args->dstImageLayout,
                           args->regionCount,
                           args->pRegions);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyBufferToImage recorded");
}

static void server_dispatch_vkCmdCopyImageToBuffer(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdCopyImageToBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyImageToBuffer (%u regions)", args->regionCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyImageToBuffer")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_copy_image_to_buffer(state,
                                                               args->srcImage,
                                                               args->dstBuffer,
                                                               args->regionCount,
                                                               args->pRegions)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyImageToBuffer");
    VkImage real_src = get_real_image(state, args->srcImage, "vkCmdCopyImageToBuffer");
    VkBuffer real_dst = get_real_buffer(state, args->dstBuffer, "vkCmdCopyImageToBuffer");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdCopyImageToBuffer(real_cb,
                           real_src,
                           args->srcImageLayout,
                           real_dst,
                           args->regionCount,
                           args->pRegions);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyImageToBuffer recorded");
}

static void server_dispatch_vkCmdFillBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdFillBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdFillBuffer");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdFillBuffer")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_fill_buffer(state, args->dstBuffer, args->dstOffset, args->size)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdFillBuffer");
    VkBuffer real_dst = get_real_buffer(state, args->dstBuffer, "vkCmdFillBuffer");
    if (real_cb == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdFillBuffer(real_cb, real_dst, args->dstOffset, args->size, args->data);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdFillBuffer recorded");
}

static void server_dispatch_vkCmdUpdateBuffer(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdUpdateBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdUpdateBuffer (size=%llu)", (unsigned long long)args->dataSize);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdUpdateBuffer")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_update_buffer(state,
                                                        args->dstBuffer,
                                                        args->dstOffset,
                                                        args->dataSize,
                                                        args->pData)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdUpdateBuffer");
    VkBuffer real_dst = get_real_buffer(state, args->dstBuffer, "vkCmdUpdateBuffer");
    if (real_cb == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }
    vkCmdUpdateBuffer(real_cb, real_dst, args->dstOffset, args->dataSize, args->pData);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdUpdateBuffer recorded");
}

static void server_dispatch_vkCmdClearColorImage(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdClearColorImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdClearColorImage (ranges=%u)", args->rangeCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdClearColorImage")) {
        return;
    }
    if (!server_state_bridge_validate_cmd_clear_color_image(state,
                                                            args->image,
                                                            args->rangeCount,
                                                            args->pRanges)) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdClearColorImage");
    VkImage real_image = get_real_image(state, args->image, "vkCmdClearColorImage");
    if (real_cb == VK_NULL_HANDLE || real_image == VK_NULL_HANDLE) {
        return;
    }
    vkCmdClearColorImage(real_cb,
                         real_image,
                         args->imageLayout,
                         args->pColor,
                         args->rangeCount,
                         args->pRanges);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdClearColorImage recorded");
}

static void server_dispatch_vkCmdBeginRenderPass(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdBeginRenderPass* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBeginRenderPass");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBeginRenderPass")) {
        return;
    }
    if (!args->pRenderPassBegin) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing begin info");
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBeginRenderPass");
    VkRenderPass real_rp =
        server_state_bridge_get_real_render_pass(state, args->pRenderPassBegin->renderPass);
    VkFramebuffer real_fb =
        server_state_bridge_get_real_framebuffer(state, args->pRenderPassBegin->framebuffer);
    if (real_cb == VK_NULL_HANDLE || real_rp == VK_NULL_HANDLE || real_fb == VK_NULL_HANDLE) {
        return;
    }
    VkRenderPassBeginInfo begin_info = *args->pRenderPassBegin;
    begin_info.renderPass = real_rp;
    begin_info.framebuffer = real_fb;
    vkCmdBeginRenderPass(real_cb, &begin_info, args->contents);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBeginRenderPass recorded");
}

static void server_dispatch_vkCmdEndRenderPass(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCmdEndRenderPass* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdEndRenderPass");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdEndRenderPass")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdEndRenderPass");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndRenderPass(real_cb);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdEndRenderPass recorded");
}

static void server_dispatch_vkCmdBindPipeline(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdBindPipeline* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindPipeline");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindPipeline")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdBindPipeline");
    if (!real_cb) {
        return;
    }
    VkPipeline real_pipeline = server_state_bridge_get_real_pipeline(state, args->pipeline);
    if (real_pipeline == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown pipeline");
        return;
    }
    vkCmdBindPipeline(real_cb, args->pipelineBindPoint, real_pipeline);
}

static void server_dispatch_vkCmdBindVertexBuffers(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdBindVertexBuffers* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindVertexBuffers (count=%u)",
           args->bindingCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindVertexBuffers")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBindVertexBuffers");
    if (!real_cb) {
        return;
    }
    if (args->bindingCount == 0 || !args->pBuffers || !args->pOffsets) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdBindVertexBuffers");
        return;
    }
    VkBuffer* real_buffers = calloc(args->bindingCount, sizeof(*real_buffers));
    if (!real_buffers) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for vertex buffers");
        return;
    }
    for (uint32_t i = 0; i < args->bindingCount; ++i) {
        real_buffers[i] =
            get_real_buffer(state, args->pBuffers[i], "vkCmdBindVertexBuffers");
        if (real_buffers[i] == VK_NULL_HANDLE) {
            free(real_buffers);
            return;
        }
    }
    vkCmdBindVertexBuffers(real_cb,
                           args->firstBinding,
                           args->bindingCount,
                           real_buffers,
                           args->pOffsets);
    free(real_buffers);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBindVertexBuffers recorded");
}

static void server_dispatch_vkCmdBindDescriptorSets(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdBindDescriptorSets* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindDescriptorSets (count=%u)",
           args->descriptorSetCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindDescriptorSets")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBindDescriptorSets");
    if (!real_cb) {
        return;
    }
    VkPipelineLayout real_layout =
        server_state_bridge_get_real_pipeline_layout(state, args->layout);
    if (real_layout == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown pipeline layout");
        return;
    }
    VkDescriptorSet* real_sets = NULL;
    if (args->descriptorSetCount > 0) {
        real_sets = calloc(args->descriptorSetCount, sizeof(*real_sets));
        if (!real_sets) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for descriptor sets");
            return;
        }
        for (uint32_t i = 0; i < args->descriptorSetCount; ++i) {
            real_sets[i] =
                server_state_bridge_get_real_descriptor_set(state, args->pDescriptorSets[i]);
            if (real_sets[i] == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown descriptor set %u", i);
                free(real_sets);
                return;
            }
        }
    }
    vkCmdBindDescriptorSets(real_cb,
                            args->pipelineBindPoint,
                            real_layout,
                            args->firstSet,
                            args->descriptorSetCount,
                            real_sets,
                            args->dynamicOffsetCount,
                            args->pDynamicOffsets);
    free(real_sets);
}

static void server_dispatch_vkCmdDispatch(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCmdDispatch* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDispatch (%u, %u, %u)",
           args->groupCountX, args->groupCountY, args->groupCountZ);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDispatch")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdDispatch");
    if (!real_cb) {
        return;
    }
    vkCmdDispatch(real_cb, args->groupCountX, args->groupCountY, args->groupCountZ);
}

static void server_dispatch_vkCmdSetViewport(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdSetViewport* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetViewport (count=%u)",
           args->viewportCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetViewport")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetViewport");
    if (!real_cb) {
        return;
    }
    if (!args->pViewports || args->viewportCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid viewport data");
        return;
    }
    vkCmdSetViewport(real_cb, args->firstViewport, args->viewportCount, args->pViewports);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdSetViewport recorded");
}

static void server_dispatch_vkCmdSetScissor(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdSetScissor* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetScissor (count=%u)",
           args->scissorCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetScissor")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetScissor");
    if (!real_cb) {
        return;
    }
    if (!args->pScissors || args->scissorCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid scissor data");
        return;
    }
    vkCmdSetScissor(real_cb, args->firstScissor, args->scissorCount, args->pScissors);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdSetScissor recorded");
}

static void server_dispatch_vkCmdDraw(struct vn_dispatch_context* ctx,
                                      struct vn_command_vkCmdDraw* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDraw (verts=%u inst=%u)",
           args->vertexCount, args->instanceCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDraw")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDraw");
    if (!real_cb) {
        return;
    }
    vkCmdDraw(real_cb,
              args->vertexCount,
              args->instanceCount,
              args->firstVertex,
              args->firstInstance);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDraw recorded");
}

static void server_dispatch_vkCmdPipelineBarrier(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdPipelineBarrier* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPipelineBarrier");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdPipelineBarrier")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdPipelineBarrier");
    if (!real_cb) {
        return;
    }

    VkBufferMemoryBarrier* buffer_barriers = NULL;
    VkImageMemoryBarrier* image_barriers = NULL;

    if (args->bufferMemoryBarrierCount > 0) {
        buffer_barriers =
            calloc(args->bufferMemoryBarrierCount, sizeof(*buffer_barriers));
        if (!buffer_barriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for buffer barriers");
            return;
        }
        for (uint32_t i = 0; i < args->bufferMemoryBarrierCount; ++i) {
            buffer_barriers[i] = args->pBufferMemoryBarriers[i];
            buffer_barriers[i].buffer = server_state_bridge_get_real_buffer(
                state, args->pBufferMemoryBarriers[i].buffer);
            if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown buffer in barrier %u", i);
                free(buffer_barriers);
                return;
            }
        }
    }

    if (args->imageMemoryBarrierCount > 0) {
        image_barriers =
            calloc(args->imageMemoryBarrierCount, sizeof(*image_barriers));
        if (!image_barriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for image barriers");
            free(buffer_barriers);
            return;
        }
        for (uint32_t i = 0; i < args->imageMemoryBarrierCount; ++i) {
            image_barriers[i] = args->pImageMemoryBarriers[i];
            image_barriers[i].image = server_state_bridge_get_real_image(
                state, args->pImageMemoryBarriers[i].image);
            if (image_barriers[i].image == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown image in barrier %u", i);
                free(buffer_barriers);
                free(image_barriers);
                return;
            }
        }
    }

    vkCmdPipelineBarrier(real_cb,
                         args->srcStageMask,
                         args->dstStageMask,
                         args->dependencyFlags,
                         args->memoryBarrierCount,
                         args->pMemoryBarriers,
                         args->bufferMemoryBarrierCount,
                         buffer_barriers,
                         args->imageMemoryBarrierCount,
                         image_barriers);
    free(buffer_barriers);
    free(image_barriers);
}

static void server_dispatch_vkCreateFence(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCreateFence* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateFence");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;
    if (!args->pFence || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkFence handle = server_state_bridge_create_fence(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    *args->pFence = handle;
}

static void server_dispatch_vkDestroyFence(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkDestroyFence* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyFence");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_destroy_fence(state, args->fence);
}

static void server_dispatch_vkGetFenceStatus(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkGetFenceStatus* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetFenceStatus");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_get_fence_status(state, args->fence);
}

static void server_dispatch_vkResetFences(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkResetFences* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetFences");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_fences(state, args->fenceCount, args->pFences);
}

static void server_dispatch_vkWaitForFences(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkWaitForFences* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkWaitForFences");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_wait_for_fences(state,
                                                    args->fenceCount,
                                                    args->pFences,
                                                    args->waitAll,
                                                    args->timeout);
}

static void server_dispatch_vkCreateSemaphore(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCreateSemaphore* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateSemaphore");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;
    if (!args->pSemaphore || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkSemaphore handle = server_state_bridge_create_semaphore(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    *args->pSemaphore = handle;
}

static void server_dispatch_vkDestroySemaphore(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkDestroySemaphore* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroySemaphore");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_destroy_semaphore(state, args->semaphore);
}

static void server_dispatch_vkGetSemaphoreCounterValue(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkGetSemaphoreCounterValue* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetSemaphoreCounterValue");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pValue) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    args->ret = server_state_bridge_get_semaphore_counter_value(state, args->semaphore, args->pValue);
}

static void server_dispatch_vkSignalSemaphore(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkSignalSemaphore* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkSignalSemaphore");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_signal_semaphore(state, args->pSignalInfo);
}

static void server_dispatch_vkWaitSemaphores(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkWaitSemaphores* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkWaitSemaphores");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_wait_semaphores(state, args->pWaitInfo, args->timeout);
}

static void server_dispatch_vkQueueSubmit(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkQueueSubmit* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkQueueSubmit (submitCount=%u)", args->submitCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_submit(state, args->queue, args->submitCount, args->pSubmits, args->fence);
}

static void server_dispatch_vkQueueWaitIdle(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkQueueWaitIdle* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkQueueWaitIdle");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_wait_idle(state, args->queue);
}

static void server_dispatch_vkDeviceWaitIdle(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkDeviceWaitIdle* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDeviceWaitIdle");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_device_wait_idle(state, args->device);
}

struct VenusRenderer* venus_renderer_create(struct ServerState* state) {
    struct VenusRenderer* renderer = (struct VenusRenderer*)calloc(1, sizeof(*renderer));
    if (!renderer)
        return NULL;

    renderer->state = state;
    renderer->decoder = vn_cs_decoder_create();
    renderer->encoder = vn_cs_encoder_create();
    if (!renderer->decoder || !renderer->encoder) {
        vn_cs_decoder_destroy(renderer->decoder);
        vn_cs_encoder_destroy(renderer->encoder);
        free(renderer);
        return NULL;
    }

    renderer->ctx.data = state;
    renderer->ctx.debug_log = NULL;
    renderer->ctx.encoder = renderer->encoder;
    renderer->ctx.decoder = renderer->decoder;

    // Phase 2 handlers
    renderer->ctx.dispatch_vkCreateInstance = server_dispatch_vkCreateInstance;
    renderer->ctx.dispatch_vkDestroyInstance = server_dispatch_vkDestroyInstance;
    renderer->ctx.dispatch_vkEnumerateInstanceVersion = server_dispatch_vkEnumerateInstanceVersion;
    renderer->ctx.dispatch_vkEnumerateInstanceExtensionProperties =
        server_dispatch_vkEnumerateInstanceExtensionProperties;
    renderer->ctx.dispatch_vkEnumerateInstanceLayerProperties = server_dispatch_vkEnumerateInstanceLayerProperties;
    renderer->ctx.dispatch_vkEnumeratePhysicalDevices = server_dispatch_vkEnumeratePhysicalDevices;

    // Phase 3 handlers: Physical device queries
    renderer->ctx.dispatch_vkGetPhysicalDeviceProperties = server_dispatch_vkGetPhysicalDeviceProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFeatures = server_dispatch_vkGetPhysicalDeviceFeatures;
    renderer->ctx.dispatch_vkGetPhysicalDeviceQueueFamilyProperties = server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceMemoryProperties = server_dispatch_vkGetPhysicalDeviceMemoryProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFormatProperties = server_dispatch_vkGetPhysicalDeviceFormatProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceImageFormatProperties = server_dispatch_vkGetPhysicalDeviceImageFormatProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceImageFormatProperties2 = server_dispatch_vkGetPhysicalDeviceImageFormatProperties2;
    renderer->ctx.dispatch_vkGetPhysicalDeviceProperties2 = server_dispatch_vkGetPhysicalDeviceProperties2;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFeatures2 = server_dispatch_vkGetPhysicalDeviceFeatures2;
    renderer->ctx.dispatch_vkGetPhysicalDeviceQueueFamilyProperties2 = server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties2;
    renderer->ctx.dispatch_vkGetPhysicalDeviceMemoryProperties2 = server_dispatch_vkGetPhysicalDeviceMemoryProperties2;
    renderer->ctx.dispatch_vkEnumerateDeviceExtensionProperties = server_dispatch_vkEnumerateDeviceExtensionProperties;
    renderer->ctx.dispatch_vkEnumerateDeviceLayerProperties = server_dispatch_vkEnumerateDeviceLayerProperties;

    // Phase 3 handlers: Device management
    renderer->ctx.dispatch_vkCreateDevice = server_dispatch_vkCreateDevice;
    renderer->ctx.dispatch_vkDestroyDevice = server_dispatch_vkDestroyDevice;
    renderer->ctx.dispatch_vkGetDeviceQueue = server_dispatch_vkGetDeviceQueue;

    // Phase 4 handlers: Memory and resources
    renderer->ctx.dispatch_vkAllocateMemory = server_dispatch_vkAllocateMemory;
    renderer->ctx.dispatch_vkFreeMemory = server_dispatch_vkFreeMemory;
    renderer->ctx.dispatch_vkCreateBuffer = server_dispatch_vkCreateBuffer;
    renderer->ctx.dispatch_vkDestroyBuffer = server_dispatch_vkDestroyBuffer;
    renderer->ctx.dispatch_vkGetBufferMemoryRequirements = server_dispatch_vkGetBufferMemoryRequirements;
    renderer->ctx.dispatch_vkBindBufferMemory = server_dispatch_vkBindBufferMemory;
    renderer->ctx.dispatch_vkCreateImage = server_dispatch_vkCreateImage;
    renderer->ctx.dispatch_vkDestroyImage = server_dispatch_vkDestroyImage;
    renderer->ctx.dispatch_vkGetImageMemoryRequirements = server_dispatch_vkGetImageMemoryRequirements;
    renderer->ctx.dispatch_vkBindImageMemory = server_dispatch_vkBindImageMemory;
    renderer->ctx.dispatch_vkGetImageSubresourceLayout = server_dispatch_vkGetImageSubresourceLayout;
    renderer->ctx.dispatch_vkCreateImageView = server_dispatch_vkCreateImageView;
    renderer->ctx.dispatch_vkDestroyImageView = server_dispatch_vkDestroyImageView;
    renderer->ctx.dispatch_vkCreateBufferView = server_dispatch_vkCreateBufferView;
    renderer->ctx.dispatch_vkDestroyBufferView = server_dispatch_vkDestroyBufferView;
    renderer->ctx.dispatch_vkCreateSampler = server_dispatch_vkCreateSampler;
    renderer->ctx.dispatch_vkDestroySampler = server_dispatch_vkDestroySampler;
    renderer->ctx.dispatch_vkCreateShaderModule = server_dispatch_vkCreateShaderModule;
    renderer->ctx.dispatch_vkDestroyShaderModule = server_dispatch_vkDestroyShaderModule;
    renderer->ctx.dispatch_vkCreateDescriptorSetLayout = server_dispatch_vkCreateDescriptorSetLayout;
    renderer->ctx.dispatch_vkDestroyDescriptorSetLayout = server_dispatch_vkDestroyDescriptorSetLayout;
    renderer->ctx.dispatch_vkCreateDescriptorPool = server_dispatch_vkCreateDescriptorPool;
    renderer->ctx.dispatch_vkDestroyDescriptorPool = server_dispatch_vkDestroyDescriptorPool;
    renderer->ctx.dispatch_vkResetDescriptorPool = server_dispatch_vkResetDescriptorPool;
    renderer->ctx.dispatch_vkAllocateDescriptorSets = server_dispatch_vkAllocateDescriptorSets;
    renderer->ctx.dispatch_vkFreeDescriptorSets = server_dispatch_vkFreeDescriptorSets;
    renderer->ctx.dispatch_vkUpdateDescriptorSets = server_dispatch_vkUpdateDescriptorSets;
    renderer->ctx.dispatch_vkCreatePipelineLayout = server_dispatch_vkCreatePipelineLayout;
    renderer->ctx.dispatch_vkDestroyPipelineLayout = server_dispatch_vkDestroyPipelineLayout;
    renderer->ctx.dispatch_vkCreateRenderPass = server_dispatch_vkCreateRenderPass;
    renderer->ctx.dispatch_vkCreateRenderPass2 = server_dispatch_vkCreateRenderPass2;
    renderer->ctx.dispatch_vkDestroyRenderPass = server_dispatch_vkDestroyRenderPass;
    renderer->ctx.dispatch_vkCreateFramebuffer = server_dispatch_vkCreateFramebuffer;
    renderer->ctx.dispatch_vkDestroyFramebuffer = server_dispatch_vkDestroyFramebuffer;
    renderer->ctx.dispatch_vkCreateComputePipelines = server_dispatch_vkCreateComputePipelines;
    renderer->ctx.dispatch_vkCreateGraphicsPipelines = server_dispatch_vkCreateGraphicsPipelines;
    renderer->ctx.dispatch_vkDestroyPipeline = server_dispatch_vkDestroyPipeline;
    renderer->ctx.dispatch_vkCreateCommandPool = server_dispatch_vkCreateCommandPool;
    renderer->ctx.dispatch_vkDestroyCommandPool = server_dispatch_vkDestroyCommandPool;
    renderer->ctx.dispatch_vkResetCommandPool = server_dispatch_vkResetCommandPool;
    renderer->ctx.dispatch_vkAllocateCommandBuffers = server_dispatch_vkAllocateCommandBuffers;
    renderer->ctx.dispatch_vkFreeCommandBuffers = server_dispatch_vkFreeCommandBuffers;
    renderer->ctx.dispatch_vkBeginCommandBuffer = server_dispatch_vkBeginCommandBuffer;
    renderer->ctx.dispatch_vkEndCommandBuffer = server_dispatch_vkEndCommandBuffer;
    renderer->ctx.dispatch_vkResetCommandBuffer = server_dispatch_vkResetCommandBuffer;
    renderer->ctx.dispatch_vkCmdCopyBuffer = server_dispatch_vkCmdCopyBuffer;
    renderer->ctx.dispatch_vkCmdCopyImage = server_dispatch_vkCmdCopyImage;
    renderer->ctx.dispatch_vkCmdBlitImage = server_dispatch_vkCmdBlitImage;
    renderer->ctx.dispatch_vkCmdCopyBufferToImage = server_dispatch_vkCmdCopyBufferToImage;
    renderer->ctx.dispatch_vkCmdCopyImageToBuffer = server_dispatch_vkCmdCopyImageToBuffer;
    renderer->ctx.dispatch_vkCmdFillBuffer = server_dispatch_vkCmdFillBuffer;
    renderer->ctx.dispatch_vkCmdUpdateBuffer = server_dispatch_vkCmdUpdateBuffer;
    renderer->ctx.dispatch_vkCmdClearColorImage = server_dispatch_vkCmdClearColorImage;
    renderer->ctx.dispatch_vkCmdBeginRenderPass = server_dispatch_vkCmdBeginRenderPass;
    renderer->ctx.dispatch_vkCmdEndRenderPass = server_dispatch_vkCmdEndRenderPass;
    renderer->ctx.dispatch_vkCmdBindPipeline = server_dispatch_vkCmdBindPipeline;
    renderer->ctx.dispatch_vkCmdBindVertexBuffers = server_dispatch_vkCmdBindVertexBuffers;
    renderer->ctx.dispatch_vkCmdBindDescriptorSets = server_dispatch_vkCmdBindDescriptorSets;
    renderer->ctx.dispatch_vkCmdDispatch = server_dispatch_vkCmdDispatch;
    renderer->ctx.dispatch_vkCmdSetViewport = server_dispatch_vkCmdSetViewport;
    renderer->ctx.dispatch_vkCmdSetScissor = server_dispatch_vkCmdSetScissor;
    renderer->ctx.dispatch_vkCmdDraw = server_dispatch_vkCmdDraw;
    renderer->ctx.dispatch_vkCmdPipelineBarrier = server_dispatch_vkCmdPipelineBarrier;
    renderer->ctx.dispatch_vkCreateFence = server_dispatch_vkCreateFence;
    renderer->ctx.dispatch_vkDestroyFence = server_dispatch_vkDestroyFence;
    renderer->ctx.dispatch_vkGetFenceStatus = server_dispatch_vkGetFenceStatus;
    renderer->ctx.dispatch_vkResetFences = server_dispatch_vkResetFences;
    renderer->ctx.dispatch_vkWaitForFences = server_dispatch_vkWaitForFences;
    renderer->ctx.dispatch_vkCreateSemaphore = server_dispatch_vkCreateSemaphore;
    renderer->ctx.dispatch_vkDestroySemaphore = server_dispatch_vkDestroySemaphore;
    renderer->ctx.dispatch_vkGetSemaphoreCounterValue = server_dispatch_vkGetSemaphoreCounterValue;
    renderer->ctx.dispatch_vkSignalSemaphore = server_dispatch_vkSignalSemaphore;
    renderer->ctx.dispatch_vkWaitSemaphores = server_dispatch_vkWaitSemaphores;
    renderer->ctx.dispatch_vkQueueSubmit = server_dispatch_vkQueueSubmit;
    renderer->ctx.dispatch_vkQueueWaitIdle = server_dispatch_vkQueueWaitIdle;
    renderer->ctx.dispatch_vkDeviceWaitIdle = server_dispatch_vkDeviceWaitIdle;

    return renderer;
}

void venus_renderer_destroy(struct VenusRenderer* renderer) {
    if (!renderer)
        return;
    vn_cs_decoder_destroy(renderer->decoder);
    vn_cs_encoder_destroy(renderer->encoder);
    free(renderer);
}

bool venus_renderer_handle(struct VenusRenderer* renderer,
                           const void* data,
                           size_t size,
                           uint8_t** reply_data,
                           size_t* reply_size) {
    if (!renderer)
        return false;

    vn_cs_decoder_init(renderer->decoder, data, size);
    vn_cs_encoder_init_dynamic(renderer->encoder);

    vn_dispatch_command(&renderer->ctx);

    if (vn_cs_decoder_get_fatal(renderer->decoder)) {
        vn_cs_decoder_reset_temp_storage(renderer->decoder);
        return false;
    }

    const size_t len = vn_cs_encoder_get_len(renderer->encoder);
    if (len == 0) {
        *reply_data = NULL;
        *reply_size = 0;
        vn_cs_decoder_reset_temp_storage(renderer->decoder);
        return true;
    }

    const uint8_t* src = vn_cs_encoder_get_data(renderer->encoder);
    uint8_t* dst = (uint8_t*)malloc(len);
    if (!dst) {
        vn_cs_decoder_reset_temp_storage(renderer->decoder);
        return false;
    }
    memcpy(dst, src, len);

    *reply_data = dst;
    *reply_size = len;

    vn_cs_decoder_reset_temp_storage(renderer->decoder);
    return true;
}
