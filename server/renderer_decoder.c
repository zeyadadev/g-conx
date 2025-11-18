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

static bool command_buffer_recording_guard(struct ServerState* state,
                                           VkCommandBuffer command_buffer,
                                           const char* name) {
    if (!server_state_bridge_command_buffer_is_recording(state, command_buffer)) {
        printf("[Venus Server]   -> ERROR: %s requires command buffer in RECORDING state\n", name);
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
        printf("[Venus Server]   -> ERROR: Failed to translate command buffer for %s\n", name);
        server_state_bridge_mark_command_buffer_invalid(state, command_buffer);
    }
    return real;
}

static VkBuffer get_real_buffer(struct ServerState* state, VkBuffer buffer, const char* name) {
    VkBuffer real = server_state_bridge_get_real_buffer(state, buffer);
    if (real == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Failed to translate buffer for %s\n", name);
    }
    return real;
}

static VkImage get_real_image(struct ServerState* state, VkImage image, const char* name) {
    VkImage real = server_state_bridge_get_real_image(state, image);
    if (real == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Failed to translate image for %s\n", name);
    }
    return real;
}

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
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pProperties) {
        printf("[Venus Server]   -> ERROR: pProperties is NULL\n");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }
    vkGetPhysicalDeviceProperties(real_device, args->pProperties);
    printf("[Venus Server]   -> Returned real properties\n");
}

static void server_dispatch_vkGetPhysicalDeviceFeatures(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFeatures* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceFeatures\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pFeatures) {
        printf("[Venus Server]   -> ERROR: pFeatures is NULL\n");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }
    vkGetPhysicalDeviceFeatures(real_device, args->pFeatures);
    printf("[Venus Server]   -> Returned real features\n");
}

static void server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceQueueFamilyProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceQueueFamilyProperties\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pQueueFamilyPropertyCount) {
        printf("[Venus Server]   -> ERROR: pQueueFamilyPropertyCount is NULL\n");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(real_device,
                                             args->pQueueFamilyPropertyCount,
                                             args->pQueueFamilyProperties);
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
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryProperties) {
        printf("[Venus Server]   -> ERROR: pMemoryProperties is NULL\n");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }
    vkGetPhysicalDeviceMemoryProperties(real_device, args->pMemoryProperties);
    printf("[Venus Server]   -> Returned real memory properties\n");
}

static void server_dispatch_vkGetPhysicalDeviceFormatProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFormatProperties* args) {
    printf("[Venus Server] Dispatching vkGetPhysicalDeviceFormatProperties (format: %d)\n", args->format);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pFormatProperties) {
        printf("[Venus Server]   -> ERROR: pFormatProperties is NULL\n");
        return;
    }
    VkPhysicalDevice real_device =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }
    vkGetPhysicalDeviceFormatProperties(real_device, args->format, args->pFormatProperties);
    printf("[Venus Server]   -> Returned real format properties\n");
}

// Phase 3: Device creation/destruction handlers
static void server_dispatch_vkCreateDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDevice* args) {
    printf("[Venus Server] Dispatching vkCreateDevice (physical device: %p)\n", (void*)args->physicalDevice);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pDevice || !args->pCreateInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: pDevice or pCreateInfo is NULL\n");
        return;
    }

    VkPhysicalDevice real_physical =
        server_state_bridge_get_real_physical_device(state, args->physicalDevice);
    if (real_physical == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Unknown physical device\n");
        return;
    }

    VkDevice real_device = VK_NULL_HANDLE;
    VkResult create_result = vkCreateDevice(real_physical, args->pCreateInfo, args->pAllocator, &real_device);
    if (create_result != VK_SUCCESS) {
        args->ret = create_result;
        printf("[Venus Server]   -> ERROR: vkCreateDevice failed: %d\n", create_result);
        return;
    }

    VkDevice client_handle =
        server_state_bridge_alloc_device(state, args->physicalDevice, real_device);
    if (client_handle == VK_NULL_HANDLE) {
        vkDestroyDevice(real_device, args->pAllocator);
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Failed to allocate server device handle\n");
        return;
    }

    *args->pDevice = client_handle;
    printf("[Venus Server]   -> Created device handle: %p\n", (void*)*args->pDevice);
}

static void server_dispatch_vkDestroyDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDevice* args) {
    printf("[Venus Server] Dispatching vkDestroyDevice (handle: %p)\n", (void*)args->device);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->device != VK_NULL_HANDLE && server_state_bridge_device_exists(state, args->device)) {
        VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
        if (real_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(real_device);
            vkDestroyDevice(real_device, args->pAllocator);
        }
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
        VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
        if (real_device == VK_NULL_HANDLE) {
            printf("[Venus Server]   -> ERROR: Unknown device\n");
            return;
        }
        VkQueue real_queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(real_device, args->queueFamilyIndex, args->queueIndex, &real_queue);
        if (real_queue == VK_NULL_HANDLE) {
            printf("[Venus Server]   -> ERROR: vkGetDeviceQueue failed\n");
            return;
        }
        *args->pQueue = server_state_bridge_alloc_queue(
            state, args->device, args->queueFamilyIndex, args->queueIndex, real_queue);
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

static void server_dispatch_vkCreateShaderModule(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCreateShaderModule* args) {
    printf("[Venus Server] Dispatching vkCreateShaderModule\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pShaderModule) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing create info or output pointer\n");
        return;
    }

    VkShaderModule handle =
        server_state_bridge_create_shader_module(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Failed to create shader module\n");
        return;
    }

    *args->pShaderModule = handle;
    printf("[Venus Server]   -> Shader module created: %p\n", (void*)handle);
}

static void server_dispatch_vkDestroyShaderModule(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkDestroyShaderModule* args) {
    printf("[Venus Server] Dispatching vkDestroyShaderModule (module: %p)\n",
           (void*)args->shaderModule);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->shaderModule != VK_NULL_HANDLE) {
        server_state_bridge_destroy_shader_module(state, args->shaderModule);
    }
}

static void server_dispatch_vkCreateDescriptorSetLayout(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDescriptorSetLayout* args) {
    printf("[Venus Server] Dispatching vkCreateDescriptorSetLayout\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pSetLayout) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing create info or output pointer\n");
        return;
    }

    VkDescriptorSetLayout layout =
        server_state_bridge_create_descriptor_set_layout(state, args->device, args->pCreateInfo);
    if (layout == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Failed to create descriptor set layout\n");
        return;
    }
    *args->pSetLayout = layout;
    printf("[Venus Server]   -> Descriptor set layout created: %p\n", (void*)layout);
}

static void server_dispatch_vkDestroyDescriptorSetLayout(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDescriptorSetLayout* args) {
    printf("[Venus Server] Dispatching vkDestroyDescriptorSetLayout (layout: %p)\n",
           (void*)args->descriptorSetLayout);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->descriptorSetLayout != VK_NULL_HANDLE) {
        server_state_bridge_destroy_descriptor_set_layout(state, args->descriptorSetLayout);
    }
}

static void server_dispatch_vkCreateDescriptorPool(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCreateDescriptorPool* args) {
    printf("[Venus Server] Dispatching vkCreateDescriptorPool\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pDescriptorPool) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing create info or output pointer\n");
        return;
    }

    VkDescriptorPool pool =
        server_state_bridge_create_descriptor_pool(state, args->device, args->pCreateInfo);
    if (pool == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Failed to create descriptor pool\n");
        return;
    }
    *args->pDescriptorPool = pool;
    printf("[Venus Server]   -> Descriptor pool created: %p\n", (void*)pool);
}

static void server_dispatch_vkDestroyDescriptorPool(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkDestroyDescriptorPool* args) {
    printf("[Venus Server] Dispatching vkDestroyDescriptorPool (pool: %p)\n",
           (void*)args->descriptorPool);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->descriptorPool != VK_NULL_HANDLE) {
        server_state_bridge_destroy_descriptor_pool(state, args->descriptorPool);
    }
}

static void server_dispatch_vkResetDescriptorPool(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkResetDescriptorPool* args) {
    printf("[Venus Server] Dispatching vkResetDescriptorPool (pool: %p)\n",
           (void*)args->descriptorPool);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_descriptor_pool(state,
                                                          args->descriptorPool,
                                                          args->flags);
}

static void server_dispatch_vkAllocateDescriptorSets(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkAllocateDescriptorSets* args) {
    printf("[Venus Server] Dispatching vkAllocateDescriptorSets (count=%u)\n",
           args->pAllocateInfo ? args->pAllocateInfo->descriptorSetCount : 0);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pAllocateInfo || !args->pDescriptorSets) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing allocate info or output pointer\n");
        return;
    }

    args->ret = server_state_bridge_allocate_descriptor_sets(state,
                                                             args->device,
                                                             args->pAllocateInfo,
                                                             args->pDescriptorSets);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Descriptor sets allocated\n");
    } else {
        printf("[Venus Server]   -> ERROR: Allocation failed (%d)\n", args->ret);
    }
}

static void server_dispatch_vkFreeDescriptorSets(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkFreeDescriptorSets* args) {
    printf("[Venus Server] Dispatching vkFreeDescriptorSets (count=%u)\n",
           args->descriptorSetCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_free_descriptor_sets(state,
                                                         args->device,
                                                         args->descriptorPool,
                                                         args->descriptorSetCount,
                                                         args->pDescriptorSets);
    if (args->ret != VK_SUCCESS) {
        printf("[Venus Server]   -> ERROR: Free descriptor sets failed (%d)\n", args->ret);
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
    printf("[Venus Server] Dispatching vkUpdateDescriptorSets (writes=%u, copies=%u)\n",
           args->descriptorWriteCount,
           args->descriptorCopyCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        printf("[Venus Server]   -> ERROR: Unknown device\n");
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
            printf("[Venus Server]   -> ERROR: Out of memory for descriptor writes\n");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto cleanup;
        }
    }

    for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet* src = &args->pDescriptorWrites[i];
        writes[i] = *src;
        writes[i].dstSet = server_state_bridge_get_real_descriptor_set(state, src->dstSet);
        if (writes[i].dstSet == VK_NULL_HANDLE) {
            printf("[Venus Server]   -> ERROR: Unknown descriptor set in write %u\n", i);
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto cleanup;
        }

        if (write_uses_buffer(src->descriptorType)) {
            if (!src->pBufferInfo) {
                printf("[Venus Server]   -> ERROR: Missing buffer info in write %u\n", i);
                result = VK_ERROR_INITIALIZATION_FAILED;
                goto cleanup;
            }
            buffer_arrays[i] =
                calloc(src->descriptorCount ? src->descriptorCount : 1, sizeof(VkDescriptorBufferInfo));
            if (!buffer_arrays[i]) {
                printf("[Venus Server]   -> ERROR: Out of memory for buffer infos\n");
                result = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->descriptorCount; ++j) {
                buffer_arrays[i][j] = src->pBufferInfo[j];
                buffer_arrays[i][j].buffer =
                    server_state_bridge_get_real_buffer(state, src->pBufferInfo[j].buffer);
                if (buffer_arrays[i][j].buffer == VK_NULL_HANDLE) {
                    printf("[Venus Server]   -> ERROR: Unknown buffer in write %u\n", i);
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
            printf("[Venus Server]   -> ERROR: Out of memory for descriptor copies\n");
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
            printf("[Venus Server]   -> ERROR: Unknown descriptor set in copy %u\n", i);
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto cleanup;
        }
    }

    vkUpdateDescriptorSets(real_device,
                           args->descriptorWriteCount,
                           writes,
                           args->descriptorCopyCount,
                           copies);
    printf("[Venus Server]   -> Descriptor sets updated\n");

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
    printf("[Venus Server] Dispatching vkCreatePipelineLayout\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pPipelineLayout) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing create info or output pointer\n");
        return;
    }

    VkPipelineLayout layout =
        server_state_bridge_create_pipeline_layout(state, args->device, args->pCreateInfo);
    if (layout == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Failed to create pipeline layout\n");
        return;
    }
    *args->pPipelineLayout = layout;
    printf("[Venus Server]   -> Pipeline layout created: %p\n", (void*)layout);
}

static void server_dispatch_vkDestroyPipelineLayout(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkDestroyPipelineLayout* args) {
    printf("[Venus Server] Dispatching vkDestroyPipelineLayout (layout: %p)\n",
           (void*)args->pipelineLayout);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->pipelineLayout != VK_NULL_HANDLE) {
        server_state_bridge_destroy_pipeline_layout(state, args->pipelineLayout);
    }
}

static void server_dispatch_vkCreateComputePipelines(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCreateComputePipelines* args) {
    printf("[Venus Server] Dispatching vkCreateComputePipelines (count=%u)\n",
           args->createInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfos || !args->pPipelines) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Missing create infos or output array\n");
        return;
    }

    args->ret = server_state_bridge_create_compute_pipelines(state,
                                                             args->device,
                                                             args->pipelineCache,
                                                             args->createInfoCount,
                                                             args->pCreateInfos,
                                                             args->pPipelines);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Compute pipeline(s) created\n");
    } else {
        printf("[Venus Server]   -> ERROR: Compute pipeline creation failed (%d)\n", args->ret);
    }
}

static void server_dispatch_vkDestroyPipeline(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkDestroyPipeline* args) {
    printf("[Venus Server] Dispatching vkDestroyPipeline (pipeline: %p)\n",
           (void*)args->pipeline);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->pipeline != VK_NULL_HANDLE) {
        server_state_bridge_destroy_pipeline(state, args->pipeline);
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

static void server_dispatch_vkCreateCommandPool(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCreateCommandPool* args) {
    printf("[Venus Server] Dispatching vkCreateCommandPool\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pCommandPool) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> ERROR: Invalid parameters\n");
        return;
    }

    VkCommandPool handle = server_state_bridge_create_command_pool(state, args->device, args->pCreateInfo);
    if (handle == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        printf("[Venus Server]   -> Failed to allocate command pool\n");
        return;
    }
    *args->pCommandPool = handle;
    printf("[Venus Server]   -> Created command pool: %p\n", (void*)handle);
}

static void server_dispatch_vkDestroyCommandPool(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkDestroyCommandPool* args) {
    printf("[Venus Server] Dispatching vkDestroyCommandPool\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!server_state_bridge_destroy_command_pool(state, args->commandPool)) {
        printf("[Venus Server]   -> Warning: Command pool not found\n");
    } else {
        printf("[Venus Server]   -> Command pool destroyed\n");
    }
}

static void server_dispatch_vkResetCommandPool(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkResetCommandPool* args) {
    printf("[Venus Server] Dispatching vkResetCommandPool\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_command_pool(state, args->commandPool, args->flags);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Command pool reset\n");
    } else {
        printf("[Venus Server]   -> Failed to reset command pool (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkAllocateCommandBuffers(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkAllocateCommandBuffers* args) {
    printf("[Venus Server] Dispatching vkAllocateCommandBuffers (count=%u)\n", args->pAllocateInfo ? args->pAllocateInfo->commandBufferCount : 0);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_allocate_command_buffers(state, args->device, args->pAllocateInfo, args->pCommandBuffers);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Command buffers allocated\n");
    } else {
        printf("[Venus Server]   -> Allocation failed (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkFreeCommandBuffers(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkFreeCommandBuffers* args) {
    printf("[Venus Server] Dispatching vkFreeCommandBuffers (count=%u)\n", args->commandBufferCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_free_command_buffers(state, args->commandPool, args->commandBufferCount, args->pCommandBuffers);
    printf("[Venus Server]   -> Command buffers freed\n");
}

static void server_dispatch_vkBeginCommandBuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkBeginCommandBuffer* args) {
    printf("[Venus Server] Dispatching vkBeginCommandBuffer (%p)\n", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_begin_command_buffer(state, args->commandBuffer, args->pBeginInfo);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Command buffer recording started\n");
    } else {
        printf("[Venus Server]   -> Failed to begin command buffer (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkEndCommandBuffer(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkEndCommandBuffer* args) {
    printf("[Venus Server] Dispatching vkEndCommandBuffer (%p)\n", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_end_command_buffer(state, args->commandBuffer);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Command buffer ended\n");
    } else {
        printf("[Venus Server]   -> Failed to end command buffer (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkResetCommandBuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkResetCommandBuffer* args) {
    printf("[Venus Server] Dispatching vkResetCommandBuffer (%p)\n", (void*)args->commandBuffer);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_command_buffer(state, args->commandBuffer, args->flags);
    if (args->ret == VK_SUCCESS) {
        printf("[Venus Server]   -> Command buffer reset\n");
    } else {
        printf("[Venus Server]   -> Failed to reset command buffer (result=%d)\n", args->ret);
    }
}

static void server_dispatch_vkCmdCopyBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdCopyBuffer* args) {
    printf("[Venus Server] Dispatching vkCmdCopyBuffer (%u regions)\n", args->regionCount);
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
    printf("[Venus Server]   -> vkCmdCopyBuffer recorded\n");
}

static void server_dispatch_vkCmdCopyImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCmdCopyImage* args) {
    printf("[Venus Server] Dispatching vkCmdCopyImage (%u regions)\n", args->regionCount);
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
    printf("[Venus Server]   -> vkCmdCopyImage recorded\n");
}

static void server_dispatch_vkCmdBlitImage(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCmdBlitImage* args) {
    printf("[Venus Server] Dispatching vkCmdBlitImage (%u regions)\n", args->regionCount);
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
    printf("[Venus Server]   -> vkCmdBlitImage recorded\n");
}

static void server_dispatch_vkCmdCopyBufferToImage(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdCopyBufferToImage* args) {
    printf("[Venus Server] Dispatching vkCmdCopyBufferToImage (%u regions)\n", args->regionCount);
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
    printf("[Venus Server]   -> vkCmdCopyBufferToImage recorded\n");
}

static void server_dispatch_vkCmdCopyImageToBuffer(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdCopyImageToBuffer* args) {
    printf("[Venus Server] Dispatching vkCmdCopyImageToBuffer (%u regions)\n", args->regionCount);
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
    printf("[Venus Server]   -> vkCmdCopyImageToBuffer recorded\n");
}

static void server_dispatch_vkCmdFillBuffer(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdFillBuffer* args) {
    printf("[Venus Server] Dispatching vkCmdFillBuffer\n");
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
    printf("[Venus Server]   -> vkCmdFillBuffer recorded\n");
}

static void server_dispatch_vkCmdUpdateBuffer(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdUpdateBuffer* args) {
    printf("[Venus Server] Dispatching vkCmdUpdateBuffer (size=%llu)\n", (unsigned long long)args->dataSize);
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
    printf("[Venus Server]   -> vkCmdUpdateBuffer recorded\n");
}

static void server_dispatch_vkCmdClearColorImage(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdClearColorImage* args) {
    printf("[Venus Server] Dispatching vkCmdClearColorImage (ranges=%u)\n", args->rangeCount);
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
    printf("[Venus Server]   -> vkCmdClearColorImage recorded\n");
}

static void server_dispatch_vkCmdBindPipeline(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdBindPipeline* args) {
    printf("[Venus Server] Dispatching vkCmdBindPipeline\n");
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
        printf("[Venus Server]   -> ERROR: Unknown pipeline\n");
        return;
    }
    vkCmdBindPipeline(real_cb, args->pipelineBindPoint, real_pipeline);
}

static void server_dispatch_vkCmdBindDescriptorSets(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdBindDescriptorSets* args) {
    printf("[Venus Server] Dispatching vkCmdBindDescriptorSets (count=%u)\n",
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
        printf("[Venus Server]   -> ERROR: Unknown pipeline layout\n");
        return;
    }
    VkDescriptorSet* real_sets = NULL;
    if (args->descriptorSetCount > 0) {
        real_sets = calloc(args->descriptorSetCount, sizeof(*real_sets));
        if (!real_sets) {
            printf("[Venus Server]   -> ERROR: Out of memory for descriptor sets\n");
            return;
        }
        for (uint32_t i = 0; i < args->descriptorSetCount; ++i) {
            real_sets[i] =
                server_state_bridge_get_real_descriptor_set(state, args->pDescriptorSets[i]);
            if (real_sets[i] == VK_NULL_HANDLE) {
                printf("[Venus Server]   -> ERROR: Unknown descriptor set %u\n", i);
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
    printf("[Venus Server] Dispatching vkCmdDispatch (%u, %u, %u)\n",
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

static void server_dispatch_vkCmdPipelineBarrier(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdPipelineBarrier* args) {
    printf("[Venus Server] Dispatching vkCmdPipelineBarrier\n");
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
            printf("[Venus Server]   -> ERROR: Out of memory for buffer barriers\n");
            return;
        }
        for (uint32_t i = 0; i < args->bufferMemoryBarrierCount; ++i) {
            buffer_barriers[i] = args->pBufferMemoryBarriers[i];
            buffer_barriers[i].buffer = server_state_bridge_get_real_buffer(
                state, args->pBufferMemoryBarriers[i].buffer);
            if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
                printf("[Venus Server]   -> ERROR: Unknown buffer in barrier %u\n", i);
                free(buffer_barriers);
                return;
            }
        }
    }

    if (args->imageMemoryBarrierCount > 0) {
        image_barriers =
            calloc(args->imageMemoryBarrierCount, sizeof(*image_barriers));
        if (!image_barriers) {
            printf("[Venus Server]   -> ERROR: Out of memory for image barriers\n");
            free(buffer_barriers);
            return;
        }
        for (uint32_t i = 0; i < args->imageMemoryBarrierCount; ++i) {
            image_barriers[i] = args->pImageMemoryBarriers[i];
            image_barriers[i].image = server_state_bridge_get_real_image(
                state, args->pImageMemoryBarriers[i].image);
            if (image_barriers[i].image == VK_NULL_HANDLE) {
                printf("[Venus Server]   -> ERROR: Unknown image in barrier %u\n", i);
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
    printf("[Venus Server] Dispatching vkCreateFence\n");
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
    printf("[Venus Server] Dispatching vkDestroyFence\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_destroy_fence(state, args->fence);
}

static void server_dispatch_vkGetFenceStatus(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkGetFenceStatus* args) {
    printf("[Venus Server] Dispatching vkGetFenceStatus\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_get_fence_status(state, args->fence);
}

static void server_dispatch_vkResetFences(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkResetFences* args) {
    printf("[Venus Server] Dispatching vkResetFences\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_fences(state, args->fenceCount, args->pFences);
}

static void server_dispatch_vkWaitForFences(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkWaitForFences* args) {
    printf("[Venus Server] Dispatching vkWaitForFences\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_wait_for_fences(state,
                                                    args->fenceCount,
                                                    args->pFences,
                                                    args->waitAll,
                                                    args->timeout);
}

static void server_dispatch_vkCreateSemaphore(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCreateSemaphore* args) {
    printf("[Venus Server] Dispatching vkCreateSemaphore\n");
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
    printf("[Venus Server] Dispatching vkDestroySemaphore\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_destroy_semaphore(state, args->semaphore);
}

static void server_dispatch_vkGetSemaphoreCounterValue(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkGetSemaphoreCounterValue* args) {
    printf("[Venus Server] Dispatching vkGetSemaphoreCounterValue\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pValue) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    args->ret = server_state_bridge_get_semaphore_counter_value(state, args->semaphore, args->pValue);
}

static void server_dispatch_vkSignalSemaphore(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkSignalSemaphore* args) {
    printf("[Venus Server] Dispatching vkSignalSemaphore\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_signal_semaphore(state, args->pSignalInfo);
}

static void server_dispatch_vkWaitSemaphores(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkWaitSemaphores* args) {
    printf("[Venus Server] Dispatching vkWaitSemaphores\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_wait_semaphores(state, args->pWaitInfo, args->timeout);
}

static void server_dispatch_vkQueueSubmit(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkQueueSubmit* args) {
    printf("[Venus Server] Dispatching vkQueueSubmit (submitCount=%u)\n", args->submitCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_submit(state, args->queue, args->submitCount, args->pSubmits, args->fence);
}

static void server_dispatch_vkQueueWaitIdle(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkQueueWaitIdle* args) {
    printf("[Venus Server] Dispatching vkQueueWaitIdle\n");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_wait_idle(state, args->queue);
}

static void server_dispatch_vkDeviceWaitIdle(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkDeviceWaitIdle* args) {
    printf("[Venus Server] Dispatching vkDeviceWaitIdle\n");
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
    renderer->ctx.dispatch_vkCreateComputePipelines = server_dispatch_vkCreateComputePipelines;
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
    renderer->ctx.dispatch_vkCmdBindPipeline = server_dispatch_vkCmdBindPipeline;
    renderer->ctx.dispatch_vkCmdBindDescriptorSets = server_dispatch_vkCmdBindDescriptorSets;
    renderer->ctx.dispatch_vkCmdDispatch = server_dispatch_vkCmdDispatch;
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
