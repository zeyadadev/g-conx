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

// Phase 4: Resource and memory management
static void server_dispatch_vkAllocateMemory(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkAllocateMemory* args) {
    printf("[Venus Server] Dispatching vkAllocateMemory\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pMemory || !args->pAllocateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pMemory or pAllocateInfo is NULL\n");
        return;
    }

    VkDeviceMemory handle = server_state_bridge_alloc_memory(state, args->device, args->pAllocateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        printf("[Venus Server]   -> ERROR: Failed to allocate memory\n");
        return;
    }

    *args->pMemory = handle;
    printf("[Venus Server]   -> Allocated memory handle: %p (size=%llu)\n",
           (void*)handle, (unsigned long long)args->pAllocateInfo->allocationSize);
}

static void server_dispatch_vkFreeMemory(struct vn_dispatch_context* ctx,
                                         struct vn_command_vkFreeMemory* args) {
    printf("[Venus Server] Dispatching vkFreeMemory (memory: %p)\n", (void*)args->memory);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->memory == VK_NULL_HANDLE) {
        return;
    }

    if (!server_state_bridge_free_memory(state, args->memory)) {
        printf("[Venus Server]   -> Warning: Memory handle not found\n");
    } else {
        printf("[Venus Server]   -> Memory freed\n");
    }
}

static void server_dispatch_vkCreateBuffer(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCreateBuffer* args) {
    printf("[Venus Server] Dispatching vkCreateBuffer (device: %p)\n", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pBuffer || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pBuffer or pCreateInfo is NULL\n");
        return;
    }

    VkBuffer handle = server_state_bridge_create_buffer(state, args->device, args->pCreateInfo);
    *args->pBuffer = handle;
    printf("[Venus Server]   -> Created buffer handle: %p (size=%llu)\n",
           (void*)handle, (unsigned long long)args->pCreateInfo->size);
}

static void server_dispatch_vkDestroyBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkDestroyBuffer* args) {
    printf("[Venus Server] Dispatching vkDestroyBuffer (buffer: %p)\n", (void*)args->buffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_buffer(state, args->buffer)) {
        printf("[Venus Server]   -> Warning: Buffer not found\n");
    } else {
        printf("[Venus Server]   -> Buffer destroyed\n");
    }
}

static void server_dispatch_vkGetBufferMemoryRequirements(struct vn_dispatch_context* ctx,
                                                          struct vn_command_vkGetBufferMemoryRequirements* args) {
    printf("[Venus Server] Dispatching vkGetBufferMemoryRequirements\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryRequirements) {
        printf("[Venus Server]   -> ERROR: pMemoryRequirements is NULL\n");
        return;
    }

    if (!server_state_bridge_get_buffer_memory_requirements(state, args->buffer, args->pMemoryRequirements)) {
        memset(args->pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        printf("[Venus Server]   -> Warning: Buffer not found\n");
    } else {
        printf("[Venus Server]   -> Requirements: size=%llu alignment=%llu\n",
               (unsigned long long)args->pMemoryRequirements->size,
               (unsigned long long)args->pMemoryRequirements->alignment);
    }
}

static void server_dispatch_vkBindBufferMemory(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkBindBufferMemory* args) {
    printf("[Venus Server] Dispatching vkBindBufferMemory (buffer: %p)\n", (void*)args->buffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_buffer_memory(state, args->buffer, args->memory, args->memoryOffset);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Buffer bound (memory=%p, offset=%llu)\n",
               (void*)args->memory, (unsigned long long)args->memoryOffset);
    } else {
        printf("[Venus Server]   -> Failed to bind buffer (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkCreateImage(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCreateImage* args) {
    printf("[Venus Server] Dispatching vkCreateImage (device: %p)\n", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pImage || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pImage or pCreateInfo is NULL\n");
        return;
    }

    VkImage handle = server_state_bridge_create_image(state, args->device, args->pCreateInfo);
    *args->pImage = handle;
    printf("[Venus Server]   -> Created image handle: %p (format=%d)\n",
           (void*)handle, args->pCreateInfo->format);
}

static void server_dispatch_vkDestroyImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkDestroyImage* args) {
    printf("[Venus Server] Dispatching vkDestroyImage (image: %p)\n", (void*)args->image);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_image(state, args->image)) {
        printf("[Venus Server]   -> Warning: Image not found\n");
    } else {
        printf("[Venus Server]   -> Image destroyed\n");
    }
}

static void server_dispatch_vkGetImageMemoryRequirements(struct vn_dispatch_context* ctx,
                                                         struct vn_command_vkGetImageMemoryRequirements* args) {
    printf("[Venus Server] Dispatching vkGetImageMemoryRequirements\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryRequirements) {
        printf("[Venus Server]   -> ERROR: pMemoryRequirements is NULL\n");
        return;
    }

    if (!server_state_bridge_get_image_memory_requirements(state, args->image, args->pMemoryRequirements)) {
        memset(args->pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        printf("[Venus Server]   -> Warning: Image not found\n");
    } else {
        printf("[Venus Server]   -> Requirements: size=%llu alignment=%llu\n",
               (unsigned long long)args->pMemoryRequirements->size,
               (unsigned long long)args->pMemoryRequirements->alignment);
    }
}

static void server_dispatch_vkBindImageMemory(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkBindImageMemory* args) {
    printf("[Venus Server] Dispatching vkBindImageMemory (image: %p)\n", (void*)args->image);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_image_memory(state, args->image, args->memory, args->memoryOffset);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Image bound (memory=%p, offset=%llu)\n",
               (void*)args->memory, (unsigned long long)args->memoryOffset);
    } else {
        printf("[Venus Server]   -> Failed to bind image (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkGetImageSubresourceLayout(struct vn_dispatch_context* ctx,
                                                        struct vn_command_vkGetImageSubresourceLayout* args) {
    printf("[Venus Server] Dispatching vkGetImageSubresourceLayout\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pLayout || !args->pSubresource) {
        printf("[Venus Server]   -> ERROR: pLayout or pSubresource is NULL\n");
        return;
    }

    if (!server_state_bridge_get_image_subresource_layout(state, args->image, args->pSubresource, args->pLayout)) {
        memset(args->pLayout, 0, sizeof(VkSubresourceLayout));
        printf("[Venus Server]   -> Warning: Image not found or invalid subresource\n");
    } else {
        printf("[Venus Server]   -> Returned subresource layout (offset=%llu)\n",
               (unsigned long long)args->pLayout->offset);
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
