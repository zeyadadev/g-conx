#define VN_RENDERER_STATIC_DISPATCH 1

#include "renderer_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server_state_bridge.h"
#include "state/fake_gpu_data_bridge.h"
#include "vn_protocol_renderer.h"
#include "vn_cs.h"

struct VenusRenderer {
    struct vn_dispatch_context ctx;
    struct vn_cs_decoder* decoder;
    struct vn_cs_encoder* encoder;
    struct ServerState* state;
};

static void server_dispatch_vkCreateInstance(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCreateInstance* args) {
    printf("[Venus Server] Dispatching vkCreateInstance\n");
    args->ret = VK_SUCCESS;
    if (!args->pInstance) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pInstance is NULL\n");
        return;
    }
    *args->pInstance = server_state_bridge_alloc_instance((struct ServerState*)ctx->data);
    printf("[Venus Server]   -> Created instance handle: %p\n", (void*)*args->pInstance);
}

static void server_dispatch_vkDestroyInstance(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkDestroyInstance* args) {
    printf("[Venus Server] Dispatching vkDestroyInstance (handle: %p)\n", (void*)args->instance);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (server_state_bridge_instance_exists(state, args->instance)) {
        server_state_bridge_remove_instance(state, args->instance);
        printf("[Venus Server]   -> Instance destroyed\n");
    } else {
        printf("[Venus Server]   -> Warning: Instance not found\n");
    }
}

static void server_dispatch_vkEnumerateInstanceVersion(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkEnumerateInstanceVersion* args) {
    printf("[Venus Server] Dispatching vkEnumerateInstanceVersion\n");
    (void)ctx;
    args->ret = VK_SUCCESS;
    if (args->pApiVersion) {
        *args->pApiVersion = VK_API_VERSION_1_3;
        printf("[Venus Server]   -> Returning API version: 1.3\n");
    }
}

static void server_dispatch_vkEnumerateInstanceExtensionProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumerateInstanceExtensionProperties* args) {
    printf("[Venus Server] Dispatching vkEnumerateInstanceExtensionProperties\n");
    (void)ctx;
    args->ret = VK_SUCCESS;
    if (args->pPropertyCount) {
        *args->pPropertyCount = 0;
        printf("[Venus Server]   -> Returning 0 extensions\n");
    }
}

static void server_dispatch_vkEnumeratePhysicalDevices(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkEnumeratePhysicalDevices* args) {
    printf("[Venus Server] Dispatching vkEnumeratePhysicalDevices (instance: %p)\n", (void*)args->instance);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pPhysicalDeviceCount) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pPhysicalDeviceCount is NULL\n");
        return;
    }

    const uint32_t available_devices = 1;
    if (!args->pPhysicalDevices) {
        *args->pPhysicalDeviceCount = available_devices;
        printf("[Venus Server]   -> Returning device count: %u\n", available_devices);
        return;
    }

    const uint32_t max_out = *args->pPhysicalDeviceCount;
    const uint32_t to_write = available_devices < max_out ? available_devices : max_out;
    for (uint32_t i = 0; i < to_write; ++i) {
        args->pPhysicalDevices[i] = server_state_bridge_get_fake_device(state);
        printf("[Venus Server]   -> Device %u: %p\n", i, (void*)args->pPhysicalDevices[i]);
    }
    *args->pPhysicalDeviceCount = to_write;

    if (max_out < available_devices) {
        args->ret = VK_INCOMPLETE;
        printf("[Venus Server]   -> Returning VK_INCOMPLETE\n");
    }
}

// Phase 3: Physical device query handlers
static void server_dispatch_vkGetPhysicalDeviceProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceProperties\n");
    (void)ctx;
    if (args->pProperties) {
        fake_gpu_data_bridge_get_properties(args->pProperties);
        printf("[Venus Server]   -> Returned fake properties\n");
    }
}

static void server_dispatch_vkGetPhysicalDeviceFeatures(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFeatures* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceFeatures\n");
    (void)ctx;
    if (args->pFeatures) {
        fake_gpu_data_bridge_get_features(args->pFeatures);
        printf("[Venus Server]   -> Returned fake features\n");
    }
}

static void server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceQueueFamilyProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceQueueFamilyProperties\n");
    (void)ctx;
    fake_gpu_data_bridge_get_queue_families(args->pQueueFamilyPropertyCount, args->pQueueFamilyProperties);
    if (args->pQueueFamilyProperties) {
        printf("[Venus Server]   -> Returned %u queue families\n", *args->pQueueFamilyPropertyCount);
    } else {
        printf("[Venus Server]   -> Returned count: %u\n", *args->pQueueFamilyPropertyCount);
    }
}

static void server_dispatch_vkGetPhysicalDeviceMemoryProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceMemoryProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceMemoryProperties\n");
    (void)ctx;
    if (args->pMemoryProperties) {
        fake_gpu_data_bridge_get_memory_properties(args->pMemoryProperties);
        printf("[Venus Server]   -> Returned fake memory properties\n");
    }
}

static void server_dispatch_vkGetPhysicalDeviceFormatProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFormatProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceFormatProperties (format: %d)\n", args->format);
    (void)ctx;
    if (args->pFormatProperties) {
        // For Phase 3, return empty format properties (no format support)
        memset(args->pFormatProperties, 0, sizeof(VkFormatProperties));
        printf("[Venus Server]   -> Returned empty format properties\n");
    }
}

// Phase 3: Device creation/destruction handlers
static void server_dispatch_vkCreateDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDevice* args) {
    printf("[Venus Server] Dispatching vkCreateDevice (physical device: %p)\n", (void*)args->physicalDevice);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pDevice) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pDevice is NULL\n");
        return;
    }

    // Allocate device handle
    *args->pDevice = server_state_bridge_alloc_device(state, args->physicalDevice);
    printf("[Venus Server]   -> Created device handle: %p\n", (void*)*args->pDevice);
}

static void server_dispatch_vkDestroyDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDevice* args) {
    printf("[Venus Server] Dispatching vkDestroyDevice (handle: %p)\n", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->device != VK_NULL_HANDLE && server_state_bridge_device_exists(state, args->device)) {
        server_state_bridge_remove_device(state, args->device);
        printf("[Venus Server]   -> Device destroyed\n");
    } else {
        printf("[Venus Server]   -> Warning: Device not found or NULL\n");
    }
}

static void server_dispatch_vkGetDeviceQueue(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceQueue* args) {
    printf("[Venus Server] Dispatching vkGetDeviceQueue (device: %p, family: %u, index: %u)\n",
           (void*)args->device, args->queueFamilyIndex, args->queueIndex);
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pQueue) {
        printf("[Venus Server]   -> ERROR: pQueue is NULL\n");
        return;
    }

    // Check if queue already exists
    VkQueue existing = server_state_bridge_find_queue(state, args->device, args->queueFamilyIndex, args->queueIndex);
    if (existing != VK_NULL_HANDLE) {
        *args->pQueue = existing;
        printf("[Venus Server]   -> Returned existing queue: %p\n", (void*)existing);
    } else {
        // Allocate new queue
        *args->pQueue = server_state_bridge_alloc_queue(state, args->device, args->queueFamilyIndex, args->queueIndex);
        printf("[Venus Server]   -> Created new queue: %p\n", (void*)*args->pQueue);
    }
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
    renderer->ctx.dispatch_vkEnumeratePhysicalDevices = server_dispatch_vkEnumeratePhysicalDevices;

    // Phase 3 handlers: Physical device queries
    renderer->ctx.dispatch_vkGetPhysicalDeviceProperties = server_dispatch_vkGetPhysicalDeviceProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFeatures = server_dispatch_vkGetPhysicalDeviceFeatures;
    renderer->ctx.dispatch_vkGetPhysicalDeviceQueueFamilyProperties = server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceMemoryProperties = server_dispatch_vkGetPhysicalDeviceMemoryProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFormatProperties = server_dispatch_vkGetPhysicalDeviceFormatProperties;

    // Phase 3 handlers: Device management
    renderer->ctx.dispatch_vkCreateDevice = server_dispatch_vkCreateDevice;
    renderer->ctx.dispatch_vkDestroyDevice = server_dispatch_vkDestroyDevice;
    renderer->ctx.dispatch_vkGetDeviceQueue = server_dispatch_vkGetDeviceQueue;

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
