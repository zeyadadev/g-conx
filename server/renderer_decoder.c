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

static const VkTimelineSemaphoreSubmitInfo* find_timeline_submit_info(const void* pNext) {
    const VkBaseInStructure* header = (const VkBaseInStructure*)pNext;
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return (const VkTimelineSemaphoreSubmitInfo*)header;
        }
        header = header->pNext;
    }
    return NULL;
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

static VkPipelineBindPoint infer_bind_point_from_stages(VkShaderStageFlags stage_flags) {
    return (stage_flags & VK_SHADER_STAGE_COMPUTE_BIT) ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                       : VK_PIPELINE_BIND_POINT_GRAPHICS;
}

static VkImage get_real_image(struct ServerState* state, VkImage image, const char* name) {
    VkImage real = server_state_bridge_get_real_image(state, image);
    if (real == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to translate image for %s", name);
    }
    return real;
}

static bool convert_dependency_info(struct ServerState* state,
                                    const VkDependencyInfo* src,
                                    VkDependencyInfo* dst,
                                    VkMemoryBarrier2** out_memory,
                                    VkBufferMemoryBarrier2** out_buffer,
                                    VkImageMemoryBarrier2** out_image,
                                    const char* name) {
    if (!src || !dst) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing dependency info", name);
        return false;
    }

    *dst = *src;
    *out_memory = NULL;
    *out_buffer = NULL;
    *out_image = NULL;

    if (src->memoryBarrierCount > 0) {
        if (!src->pMemoryBarriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing memory barriers", name);
            return false;
        }
        VkMemoryBarrier2* memory =
            calloc(src->memoryBarrierCount, sizeof(*memory));
        if (!memory) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            return false;
        }
        memcpy(memory, src->pMemoryBarriers, src->memoryBarrierCount * sizeof(*memory));
        dst->pMemoryBarriers = memory;
        *out_memory = memory;
    } else {
        dst->pMemoryBarriers = NULL;
    }

    if (src->bufferMemoryBarrierCount > 0) {
        if (!src->pBufferMemoryBarriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing buffer barriers", name);
            free(*out_memory);
            return false;
        }
        VkBufferMemoryBarrier2* buffers =
            calloc(src->bufferMemoryBarrierCount, sizeof(*buffers));
        if (!buffers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            free(*out_memory);
            return false;
        }
        memcpy(buffers,
               src->pBufferMemoryBarriers,
               src->bufferMemoryBarrierCount * sizeof(*buffers));
        for (uint32_t i = 0; i < src->bufferMemoryBarrierCount; ++i) {
            buffers[i].buffer = server_state_bridge_get_real_buffer(state, buffers[i].buffer);
            if (buffers[i].buffer == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER,
                             "[Venus Server]   -> ERROR: %s buffer barrier %u not tracked",
                             name,
                             i);
                free(*out_memory);
                free(buffers);
                return false;
            }
        }
        dst->pBufferMemoryBarriers = buffers;
        *out_buffer = buffers;
    } else {
        dst->pBufferMemoryBarriers = NULL;
    }

    if (src->imageMemoryBarrierCount > 0) {
        if (!src->pImageMemoryBarriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing image barriers", name);
            free(*out_memory);
            free(*out_buffer);
            return false;
        }
        VkImageMemoryBarrier2* images =
            calloc(src->imageMemoryBarrierCount, sizeof(*images));
        if (!images) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            free(*out_memory);
            free(*out_buffer);
            return false;
        }
        memcpy(images,
               src->pImageMemoryBarriers,
               src->imageMemoryBarrierCount * sizeof(*images));
        for (uint32_t i = 0; i < src->imageMemoryBarrierCount; ++i) {
            images[i].image = server_state_bridge_get_real_image(state, images[i].image);
            if (images[i].image == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER,
                             "[Venus Server]   -> ERROR: %s image barrier %u not tracked",
                             name,
                             i);
                free(*out_memory);
                free(*out_buffer);
                free(images);
                return false;
            }
        }
        dst->pImageMemoryBarriers = images;
        *out_image = images;
    } else {
        dst->pImageMemoryBarriers = NULL;
    }

    return true;
}

static bool translate_rendering_attachment(struct ServerState* state,
                                           VkRenderingAttachmentInfo* attachment,
                                           const char* name) {
    if (!attachment) {
        return true;
    }
    if (attachment->imageView != VK_NULL_HANDLE) {
        VkImageView real_view =
            server_state_bridge_get_real_image_view(state, attachment->imageView);
        if (real_view == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER,
                         "[Venus Server]   -> ERROR: %s attachment image view not tracked",
                         name);
            return false;
        }
        attachment->imageView = real_view;
    }
    if (attachment->resolveImageView != VK_NULL_HANDLE) {
        VkImageView real_resolve =
            server_state_bridge_get_real_image_view(state, attachment->resolveImageView);
        if (real_resolve == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER,
                         "[Venus Server]   -> ERROR: %s resolve image view not tracked",
                         name);
            return false;
        }
        attachment->resolveImageView = real_resolve;
    }
    return true;
}

static bool translate_descriptor_write(struct ServerState* state,
                                       const VkWriteDescriptorSet* src,
                                       VkWriteDescriptorSet* dst,
                                       VkDescriptorBufferInfo** out_buffers,
                                       VkDescriptorImageInfo** out_images,
                                       VkBufferView** out_texel_views,
                                       const char* name) {
    if (!src || !dst) {
        return false;
    }
    *dst = *src;
    *out_buffers = NULL;
    *out_images = NULL;
    *out_texel_views = NULL;

    switch (src->descriptorType) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        if (!src->pBufferInfo || src->descriptorCount == 0) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing buffer info", name);
            return false;
        }
        *out_buffers =
            calloc(src->descriptorCount ? src->descriptorCount : 1, sizeof(VkDescriptorBufferInfo));
        if (!*out_buffers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            return false;
        }
        for (uint32_t i = 0; i < src->descriptorCount; ++i) {
            (*out_buffers)[i] = src->pBufferInfo[i];
            (*out_buffers)[i].buffer =
                server_state_bridge_get_real_buffer(state, src->pBufferInfo[i].buffer);
            if ((*out_buffers)[i].buffer == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER,
                             "[Venus Server]   -> ERROR: Unknown buffer in %s write %u",
                             name,
                             i);
                free(*out_buffers);
                *out_buffers = NULL;
                return false;
            }
        }
        dst->pBufferInfo = *out_buffers;
        dst->pImageInfo = NULL;
        dst->pTexelBufferView = NULL;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        if (!src->pImageInfo || src->descriptorCount == 0) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing image info", name);
            return false;
        }
        *out_images =
            calloc(src->descriptorCount ? src->descriptorCount : 1, sizeof(VkDescriptorImageInfo));
        if (!*out_images) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            return false;
        }
        for (uint32_t i = 0; i < src->descriptorCount; ++i) {
            (*out_images)[i] = src->pImageInfo[i];
            if ((*out_images)[i].imageView != VK_NULL_HANDLE) {
                (*out_images)[i].imageView =
                    server_state_bridge_get_real_image_view(state, src->pImageInfo[i].imageView);
                if ((*out_images)[i].imageView == VK_NULL_HANDLE) {
                    VP_LOG_ERROR(SERVER,
                                 "[Venus Server]   -> ERROR: Unknown image view in %s write %u",
                                 name,
                                 i);
                    free(*out_images);
                    *out_images = NULL;
                    return false;
                }
            }
            if ((*out_images)[i].sampler != VK_NULL_HANDLE) {
                (*out_images)[i].sampler =
                    server_state_bridge_get_real_sampler(state, src->pImageInfo[i].sampler);
                if ((*out_images)[i].sampler == VK_NULL_HANDLE) {
                    VP_LOG_ERROR(SERVER,
                                 "[Venus Server]   -> ERROR: Unknown sampler in %s write %u",
                                 name,
                                 i);
                    free(*out_images);
                    *out_images = NULL;
                    return false;
                }
            }
        }
        dst->pBufferInfo = NULL;
        dst->pImageInfo = *out_images;
        dst->pTexelBufferView = NULL;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        if (!src->pTexelBufferView || src->descriptorCount == 0) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: %s missing texel buffer view", name);
            return false;
        }
        *out_texel_views =
            calloc(src->descriptorCount ? src->descriptorCount : 1, sizeof(VkBufferView));
        if (!*out_texel_views) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in %s", name);
            return false;
        }
        for (uint32_t i = 0; i < src->descriptorCount; ++i) {
            (*out_texel_views)[i] =
                server_state_bridge_get_real_buffer_view(state, src->pTexelBufferView[i]);
            if ((*out_texel_views)[i] == VK_NULL_HANDLE && src->pTexelBufferView[i] != VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER,
                             "[Venus Server]   -> ERROR: Unknown buffer view in %s write %u",
                             name,
                             i);
                free(*out_texel_views);
                *out_texel_views = NULL;
                return false;
            }
        }
        dst->pBufferInfo = NULL;
        dst->pImageInfo = NULL;
        dst->pTexelBufferView = *out_texel_views;
        break;
    default:
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unsupported descriptor type %d in %s",
                     src->descriptorType,
                     name);
        return false;
    }

    dst->dstSet = VK_NULL_HANDLE; // push descriptors do not use a descriptor set
    return true;
}

static VkBufferCopy* clone_buffer_copy2_array(uint32_t count, const VkBufferCopy2* src) {
    if (!count || !src)
        return NULL;
    VkBufferCopy* copies = calloc(count, sizeof(*copies));
    if (!copies)
        return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        copies[i].srcOffset = src[i].srcOffset;
        copies[i].dstOffset = src[i].dstOffset;
        copies[i].size = src[i].size;
    }
    return copies;
}

static VkImageCopy* clone_image_copy2_array(uint32_t count, const VkImageCopy2* src) {
    if (!count || !src)
        return NULL;
    VkImageCopy* copies = calloc(count, sizeof(*copies));
    if (!copies)
        return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        copies[i].srcSubresource = src[i].srcSubresource;
        copies[i].srcOffset = src[i].srcOffset;
        copies[i].dstSubresource = src[i].dstSubresource;
        copies[i].dstOffset = src[i].dstOffset;
        copies[i].extent = src[i].extent;
    }
    return copies;
}

static VkBufferImageCopy* clone_buffer_image_copy2_array(uint32_t count,
                                                         const VkBufferImageCopy2* src) {
    if (!count || !src)
        return NULL;
    VkBufferImageCopy* copies = calloc(count, sizeof(*copies));
    if (!copies)
        return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        copies[i].bufferOffset = src[i].bufferOffset;
        copies[i].bufferRowLength = src[i].bufferRowLength;
        copies[i].bufferImageHeight = src[i].bufferImageHeight;
        copies[i].imageSubresource = src[i].imageSubresource;
        copies[i].imageOffset = src[i].imageOffset;
        copies[i].imageExtent = src[i].imageExtent;
    }
    return copies;
}

static VkImageBlit* clone_image_blit2_array(uint32_t count, const VkImageBlit2* src) {
    if (!count || !src)
        return NULL;
    VkImageBlit* copies = calloc(count, sizeof(*copies));
    if (!copies)
        return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        copies[i].srcSubresource = src[i].srcSubresource;
        copies[i].dstSubresource = src[i].dstSubresource;
        memcpy(copies[i].srcOffsets, src[i].srcOffsets, sizeof(copies[i].srcOffsets));
        memcpy(copies[i].dstOffsets, src[i].dstOffsets, sizeof(copies[i].dstOffsets));
    }
    return copies;
}

static VkImageResolve* clone_image_resolve2_array(uint32_t count, const VkImageResolve2* src) {
    if (!count || !src)
        return NULL;
    VkImageResolve* copies = calloc(count, sizeof(*copies));
    if (!copies)
        return NULL;
    for (uint32_t i = 0; i < count; ++i) {
        copies[i].srcSubresource = src[i].srcSubresource;
        copies[i].srcOffset = src[i].srcOffset;
        copies[i].dstSubresource = src[i].dstSubresource;
        copies[i].dstOffset = src[i].dstOffset;
        copies[i].extent = src[i].extent;
    }
    return copies;
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
        *args->pApiVersion = VK_API_VERSION_1_4;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning API version: 1.4");
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

    *args->pPropertyCount = 0;
    if (args->pProperties && *args->pPropertyCount > 0) {
        memset(args->pProperties, 0, sizeof(VkLayerProperties) * (*args->pPropertyCount));
    }
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning zero instance layers");
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

static void server_dispatch_vkEnumeratePhysicalDeviceGroups(struct vn_dispatch_context* ctx,
                                                           struct vn_command_vkEnumeratePhysicalDeviceGroups* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkEnumeratePhysicalDeviceGroups");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pPhysicalDeviceGroupCount) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPhysicalDeviceGroupCount is NULL");
        return;
    }

    const uint32_t available_groups = 1;
    if (!args->pPhysicalDeviceGroupProperties) {
        *args->pPhysicalDeviceGroupCount = available_groups;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returning group count: %u", available_groups);
        return;
    }

    const uint32_t max_out = *args->pPhysicalDeviceGroupCount;
    const uint32_t to_write = available_groups < max_out ? available_groups : max_out;
    for (uint32_t i = 0; i < to_write; ++i) {
        VkPhysicalDeviceGroupProperties* group = &args->pPhysicalDeviceGroupProperties[i];
        group->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
        group->pNext = NULL;
        group->physicalDeviceCount = 1;
        group->physicalDevices[0] = server_state_bridge_get_fake_device(state);
        group->subsetAllocation = VK_FALSE;
        for (uint32_t j = 1; j < VK_MAX_DEVICE_GROUP_SIZE; ++j) {
            group->physicalDevices[j] = VK_NULL_HANDLE;
        }
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Group %u: device=%p", i, (void*)group->physicalDevices[0]);
    }
    *args->pPhysicalDeviceGroupCount = to_write;

    if (max_out < available_groups) {
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

static void server_dispatch_vkGetPhysicalDeviceFormatProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceFormatProperties2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPhysicalDeviceFormatProperties2 (format: %d)", args->format);
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

    vkGetPhysicalDeviceFormatProperties2(real_device, args->format, args->pFormatProperties);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned real format properties2");
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

    // Ensure Vulkan 1.4 properties report at least GENERAL layout for host copy if caller provided storage.
    VkBaseOutStructure* next = (VkBaseOutStructure*)args->pProperties->pNext;
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES) {
            VkPhysicalDeviceVulkan14Properties* vk14 =
                (VkPhysicalDeviceVulkan14Properties*)next;
            if (vk14->pCopySrcLayouts && vk14->copySrcLayoutCount == 0) {
                vk14->copySrcLayoutCount = 1;
                vk14->pCopySrcLayouts[0] = VK_IMAGE_LAYOUT_GENERAL;
            }
            if (vk14->pCopyDstLayouts && vk14->copyDstLayoutCount == 0) {
                vk14->copyDstLayoutCount = 1;
                vk14->pCopyDstLayouts[0] = VK_IMAGE_LAYOUT_GENERAL;
            }
            break;
        }
        next = next->pNext;
    }
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

    // Surface host image copy and push descriptor capability through Vulkan 1.4 feature struct.
    VkBaseOutStructure* next = (VkBaseOutStructure*)args->pFeatures->pNext;
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES) {
            VkPhysicalDeviceVulkan14Features* vk14 =
                (VkPhysicalDeviceVulkan14Features*)next;
            vk14->hostImageCopy = VK_TRUE;
            vk14->maintenance6 = VK_TRUE;
            vk14->pushDescriptor = VK_TRUE;
            vk14->maintenance5 = VK_TRUE;
            vk14->pipelineRobustness = VK_TRUE;
            vk14->pipelineProtectedAccess = VK_TRUE;
            vk14->dynamicRenderingLocalRead = VK_TRUE;
            vk14->indexTypeUint8 = VK_TRUE;
            vk14->vertexAttributeInstanceRateDivisor = VK_TRUE;
            vk14->vertexAttributeInstanceRateZeroDivisor = VK_TRUE;
            vk14->shaderSubgroupRotate = VK_TRUE;
            vk14->shaderSubgroupRotateClustered = VK_TRUE;
            vk14->shaderFloatControls2 = VK_TRUE;
            vk14->shaderExpectAssume = VK_TRUE;
            break;
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES) {
            VkPhysicalDeviceGlobalPriorityQueryFeatures* gpq =
                (VkPhysicalDeviceGlobalPriorityQueryFeatures*)next;
            gpq->globalPriorityQuery = VK_TRUE;
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES) {
            VkPhysicalDeviceDynamicRenderingLocalReadFeatures* dr =
                (VkPhysicalDeviceDynamicRenderingLocalReadFeatures*)next;
            dr->dynamicRenderingLocalRead = VK_TRUE;
        }
        next = next->pNext;
    }
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

    if (args->pQueueFamilyProperties && args->pQueueFamilyPropertyCount) {
        for (uint32_t i = 0; i < *args->pQueueFamilyPropertyCount; ++i) {
            VkBaseOutStructure* next =
                (VkBaseOutStructure*)args->pQueueFamilyProperties[i].pNext;
            while (next) {
                if (next->sType == VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES) {
                    VkQueueFamilyGlobalPriorityProperties* gp =
                        (VkQueueFamilyGlobalPriorityProperties*)next;
                    if (gp->priorityCount == 0) {
                        static const VkQueueGlobalPriority defaults[] = {
                            VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
                        };
                        gp->priorityCount = 1;
                        gp->priorities[0] = defaults[0];
                    }
                }
                next = next->pNext;
            }
        }
    }
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

static void server_dispatch_vkGetDeviceQueue2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceQueue2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceQueue2");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pQueue || !args->pQueueInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkGetDeviceQueue2");
        return;
    }

    uint32_t family_index = args->pQueueInfo->queueFamilyIndex;
    uint32_t queue_index = args->pQueueInfo->queueIndex;

    VkQueue existing = server_state_bridge_find_queue(state, args->device, family_index, queue_index);
    if (existing != VK_NULL_HANDLE) {
        *args->pQueue = existing;
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned existing queue: %p", (void*)existing);
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        return;
    }

    VkDeviceQueueInfo2 info = *args->pQueueInfo;
    VkQueue real_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue2(real_device, &info, &real_queue);
    if (real_queue == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkGetDeviceQueue2 failed");
        return;
    }

    *args->pQueue = server_state_bridge_alloc_queue(
        state, args->device, family_index, queue_index, real_queue);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Created new queue2: %p", (void*)*args->pQueue);
}

static void server_dispatch_vkGetDeviceGroupPeerMemoryFeatures(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceGroupPeerMemoryFeatures* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceGroupPeerMemoryFeatures");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pPeerMemoryFeatures) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pPeerMemoryFeatures is NULL");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        *args->pPeerMemoryFeatures = 0;
        return;
    }

    vkGetDeviceGroupPeerMemoryFeatures(real_device,
                                       args->heapIndex,
                                       args->localDeviceIndex,
                                       args->remoteDeviceIndex,
                                       args->pPeerMemoryFeatures);
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

static void server_dispatch_vkGetDeviceMemoryCommitment(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceMemoryCommitment* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceMemoryCommitment");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pCommittedMemoryInBytes) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pCommittedMemoryInBytes is NULL");
        return;
    }

    server_state_bridge_get_device_memory_commitment(state,
                                                     args->device,
                                                     args->memory,
                                                     args->pCommittedMemoryInBytes);
    VP_LOG_INFO(SERVER,
                "[Venus Server]   -> Committed bytes: %llu",
                (unsigned long long)*args->pCommittedMemoryInBytes);
}

static void server_dispatch_vkMapMemory(struct vn_dispatch_context* ctx,
                                        struct vn_command_vkMapMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkMapMemory");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->ppData) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: ppData is NULL");
        args->ret = VK_ERROR_MEMORY_MAP_FAILED;
        return;
    }

    VkDeviceMemory real_memory = server_state_bridge_get_real_memory(state, args->memory);
    if (real_memory == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown memory in vkMapMemory");
        args->ret = VK_ERROR_MEMORY_MAP_FAILED;
        return;
    }

    VkDeviceSize total_size = 0;
    server_state_bridge_get_memory_size(state, args->memory, &total_size);
    VkDeviceSize map_size = args->size == VK_WHOLE_SIZE ? (total_size > args->offset ? total_size - args->offset : 0) : args->size;
    if (args->offset + map_size > total_size) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Map range exceeds allocation");
        args->ret = VK_ERROR_MEMORY_MAP_FAILED;
        return;
    }

    args->ret = server_state_bridge_map_memory(state, args->memory, args->offset, map_size, args->flags, args->ppData);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkMapMemory failed: %d", args->ret);
    }
}

static void server_dispatch_vkUnmapMemory(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkUnmapMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkUnmapMemory");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_unmap_memory(state, args->memory);
}

static void server_dispatch_vkMapMemory2(struct vn_dispatch_context* ctx,
                                         struct vn_command_vkMapMemory2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkMapMemory2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pMemoryMapInfo || !args->ppData) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing map info or ppData");
        args->ret = VK_ERROR_MEMORY_MAP_FAILED;
        return;
    }

    VkMemoryMapInfo info = *args->pMemoryMapInfo;
    VkDeviceSize total_size = 0;
    server_state_bridge_get_memory_size(state, info.memory, &total_size);
    VkDeviceSize map_size = info.size == VK_WHOLE_SIZE ? (total_size > info.offset ? total_size - info.offset : 0) : info.size;
    if (info.offset + map_size > total_size) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Map range exceeds allocation");
        args->ret = VK_ERROR_MEMORY_MAP_FAILED;
        return;
    }

    args->ret = server_state_bridge_map_memory(state, info.memory, info.offset, map_size, info.flags, args->ppData);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkMapMemory2 failed: %d", args->ret);
    }
}

static void server_dispatch_vkUnmapMemory2(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkUnmapMemory2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkUnmapMemory2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pMemoryUnmapInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing pMemoryUnmapInfo");
        return;
    }
    server_state_bridge_unmap_memory(state, args->pMemoryUnmapInfo->memory);
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
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Requirements: size=%llu alignment=%llu memoryTypeBits=0x%x",
               (unsigned long long)args->pMemoryRequirements->size,
               (unsigned long long)args->pMemoryRequirements->alignment,
               args->pMemoryRequirements->memoryTypeBits);
    }
}

static void server_dispatch_vkGetBufferMemoryRequirements2(struct vn_dispatch_context* ctx,
                                                           struct vn_command_vkGetBufferMemoryRequirements2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetBufferMemoryRequirements2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pInfo || !args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing info or output for vkGetBufferMemoryRequirements2");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    VkBuffer real_buffer =
        get_real_buffer(state, args->pInfo->buffer, "vkGetBufferMemoryRequirements2");
    if (real_device == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device or buffer");
        return;
    }

    VkBufferMemoryRequirementsInfo2 info = *args->pInfo;
    info.buffer = real_buffer;
    vkGetBufferMemoryRequirements2(real_device, &info, args->pMemoryRequirements);
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

static void server_dispatch_vkBindBufferMemory2(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkBindBufferMemory2* args) {
    VP_LOG_INFO(SERVER,
                "[Venus Server] Dispatching vkBindBufferMemory2 (count=%u)",
                args->bindInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_buffer_memory2(state,
                                                        args->device,
                                                        args->bindInfoCount,
                                                        args->pBindInfos);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Bound %u buffer(s)", args->bindInfoCount);
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkBindBufferMemory2 failed: %d", args->ret);
    }
}

static void server_dispatch_vkGetBufferDeviceAddress(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkGetBufferDeviceAddress* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetBufferDeviceAddress");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = 0;

    if (!args->pInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pInfo is NULL for vkGetBufferDeviceAddress");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device in vkGetBufferDeviceAddress");
        return;
    }

    VkBuffer real_buffer = get_real_buffer(state, args->pInfo->buffer, "vkGetBufferDeviceAddress");
    if (real_buffer == VK_NULL_HANDLE) {
        return;
    }

    VkBufferDeviceAddressInfo info = *args->pInfo;
    info.buffer = real_buffer;
    args->ret = vkGetBufferDeviceAddress(real_device, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Device address=0x%llx", (unsigned long long)args->ret);
}

static void server_dispatch_vkGetBufferOpaqueCaptureAddress(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetBufferOpaqueCaptureAddress* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetBufferOpaqueCaptureAddress");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = 0;

    if (!args->pInfo) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: pInfo is NULL for vkGetBufferOpaqueCaptureAddress");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unknown device in vkGetBufferOpaqueCaptureAddress");
        return;
    }

    VkBuffer real_buffer =
        get_real_buffer(state, args->pInfo->buffer, "vkGetBufferOpaqueCaptureAddress");
    if (real_buffer == VK_NULL_HANDLE) {
        return;
    }

    VkBufferDeviceAddressInfo info = *args->pInfo;
    info.buffer = real_buffer;
    args->ret = vkGetBufferOpaqueCaptureAddress(real_device, &info);
    VP_LOG_INFO(SERVER,
                "[Venus Server]   -> Opaque capture address=0x%llx",
                (unsigned long long)args->ret);
}

static void server_dispatch_vkGetDeviceMemoryOpaqueCaptureAddress(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceMemoryOpaqueCaptureAddress* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceMemoryOpaqueCaptureAddress");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = 0;

    if (!args->pInfo) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: pInfo is NULL for vkGetDeviceMemoryOpaqueCaptureAddress");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unknown device in vkGetDeviceMemoryOpaqueCaptureAddress");
        return;
    }

    VkDeviceMemory real_memory =
        server_state_bridge_get_real_memory(state, args->pInfo->memory);
    if (real_memory == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Memory not tracked in vkGetDeviceMemoryOpaqueCaptureAddress");
        return;
    }

    VkDeviceMemoryOpaqueCaptureAddressInfo info = *args->pInfo;
    info.memory = real_memory;
    args->ret = vkGetDeviceMemoryOpaqueCaptureAddress(real_device, &info);
    VP_LOG_INFO(SERVER,
                "[Venus Server]   -> Memory opaque capture address=0x%llx",
                (unsigned long long)args->ret);
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

static void server_dispatch_vkGetImageMemoryRequirements2(struct vn_dispatch_context* ctx,
                                                          struct vn_command_vkGetImageMemoryRequirements2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetImageMemoryRequirements2");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pInfo || !args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing info or output for vkGetImageMemoryRequirements2");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    VkImage real_image =
        get_real_image(state, args->pInfo->image, "vkGetImageMemoryRequirements2");
    if (real_device == VK_NULL_HANDLE || real_image == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device or image");
        return;
    }

    VkImageMemoryRequirementsInfo2 info = *args->pInfo;
    info.image = real_image;
    vkGetImageMemoryRequirements2(real_device, &info, args->pMemoryRequirements);
}

static void server_dispatch_vkGetDeviceBufferMemoryRequirements(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceBufferMemoryRequirements* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceBufferMemoryRequirements");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pInfo || !args->pInfo->pCreateInfo || !args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Invalid parameters for vkGetDeviceBufferMemoryRequirements");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unknown device in vkGetDeviceBufferMemoryRequirements");
        return;
    }

    vkGetDeviceBufferMemoryRequirements(real_device, args->pInfo, args->pMemoryRequirements);
}

static void server_dispatch_vkGetDeviceImageMemoryRequirements(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceImageMemoryRequirements* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceImageMemoryRequirements");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pInfo || !args->pInfo->pCreateInfo || !args->pMemoryRequirements) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Invalid parameters for vkGetDeviceImageMemoryRequirements");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unknown device in vkGetDeviceImageMemoryRequirements");
        return;
    }

    vkGetDeviceImageMemoryRequirements(real_device, args->pInfo, args->pMemoryRequirements);
}

static void server_dispatch_vkGetDeviceImageSparseMemoryRequirements(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceImageSparseMemoryRequirements* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceImageSparseMemoryRequirements");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pInfo || !args->pInfo->pCreateInfo || !args->pSparseMemoryRequirementCount) {
        VP_LOG_ERROR(
            SERVER,
            "[Venus Server]   -> ERROR: Invalid parameters for vkGetDeviceImageSparseMemoryRequirements");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Unknown device in vkGetDeviceImageSparseMemoryRequirements");
        return;
    }

    vkGetDeviceImageSparseMemoryRequirements(real_device,
                                             args->pInfo,
                                             args->pSparseMemoryRequirementCount,
                                             args->pSparseMemoryRequirements);
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

static void server_dispatch_vkCreateDescriptorUpdateTemplate(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDescriptorUpdateTemplate* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateDescriptorUpdateTemplate");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCreateInfo || !args->pDescriptorUpdateTemplate) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }

    VkDescriptorUpdateTemplate tmpl =
        server_state_bridge_create_descriptor_update_template(state, args->device, args->pCreateInfo);
    if (tmpl == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create descriptor update template");
        return;
    }
    *args->pDescriptorUpdateTemplate = tmpl;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Descriptor update template created: %p", (void*)tmpl);
}

static void server_dispatch_vkDestroyDescriptorUpdateTemplate(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkDestroyDescriptorUpdateTemplate* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyDescriptorUpdateTemplate (template: %p)",
           (void*)args->descriptorUpdateTemplate);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->descriptorUpdateTemplate != VK_NULL_HANDLE) {
        server_state_bridge_destroy_descriptor_update_template(state, args->descriptorUpdateTemplate);
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

static void server_dispatch_vkCmdPushDescriptorSet(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdPushDescriptorSet* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushDescriptorSet (writes=%u)",
           args->descriptorWriteCount);
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdPushDescriptorSet")) {
        return;
    }

    if (args->set != 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Push descriptors support set 0 only");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cmd =
        server_state_bridge_get_real_command_buffer(state, args->commandBuffer);
    VkPipelineLayout real_layout =
        server_state_bridge_get_real_pipeline_layout(state, args->layout);

    if (real_cmd == VK_NULL_HANDLE || real_layout == VK_NULL_HANDLE) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkWriteDescriptorSet* writes = NULL;
    VkDescriptorBufferInfo** buffer_arrays = NULL;
    VkDescriptorImageInfo** image_arrays = NULL;
    VkBufferView** texel_arrays = NULL;

    if (args->descriptorWriteCount > 0) {
        writes = calloc(args->descriptorWriteCount, sizeof(*writes));
        buffer_arrays = calloc(args->descriptorWriteCount, sizeof(*buffer_arrays));
        image_arrays = calloc(args->descriptorWriteCount, sizeof(*image_arrays));
        texel_arrays = calloc(args->descriptorWriteCount, sizeof(*texel_arrays));
        if (!writes || !buffer_arrays || !image_arrays || !texel_arrays) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for push descriptors");
            goto cleanup;
        }
    }

    for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
        if (!translate_descriptor_write(state,
                                        &args->pDescriptorWrites[i],
                                        &writes[i],
                                        &buffer_arrays[i],
                                        &image_arrays[i],
                                        &texel_arrays[i],
                                        "vkCmdPushDescriptorSet")) {
            server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
            goto cleanup;
        }
    }

    VkDevice real_device = server_state_bridge_get_command_buffer_real_device(state, args->commandBuffer);
    PFN_vkCmdPushDescriptorSet fp =
        (PFN_vkCmdPushDescriptorSet)vkGetDeviceProcAddr(real_device, "vkCmdPushDescriptorSet");
    if (!fp) {
        fp = (PFN_vkCmdPushDescriptorSet)vkGetDeviceProcAddr(real_device, "vkCmdPushDescriptorSetKHR");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCmdPushDescriptorSet not supported on device");
        goto cleanup;
    }

    fp(real_cmd,
       args->pipelineBindPoint,
       real_layout,
       args->set,
       args->descriptorWriteCount,
       writes);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Push descriptors recorded");

cleanup:
    if (buffer_arrays) {
        for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
            free(buffer_arrays[i]);
        }
    }
    if (image_arrays) {
        for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
            free(image_arrays[i]);
        }
    }
    if (texel_arrays) {
        for (uint32_t i = 0; i < args->descriptorWriteCount; ++i) {
            free(texel_arrays[i]);
        }
    }
    free(writes);
    free(buffer_arrays);
    free(image_arrays);
    free(texel_arrays);
}

static void server_dispatch_vkCmdPushDescriptorSetWithTemplate(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdPushDescriptorSetWithTemplate* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushDescriptorSetWithTemplate");
    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdPushDescriptorSetWithTemplate")) {
        return;
    }

    struct DescriptorUpdateTemplateInfoBridge tmpl_info = {};
    if (!server_state_bridge_get_descriptor_update_template_info(
            state, args->descriptorUpdateTemplate, &tmpl_info)) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Template metadata not found");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    if (tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET &&
        tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS &&
        tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unsupported template type for push descriptors");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    if (args->set != tmpl_info.set_number) {
        VP_LOG_ERROR(SERVER,
                     "[Venus Server]   -> ERROR: Template set %u does not match requested set %u",
                     tmpl_info.set_number,
                     args->set);
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    if (args->set != 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Push descriptors support set 0 only");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cmd =
        server_state_bridge_get_real_command_buffer(state, args->commandBuffer);
    VkPipelineLayout real_layout =
        server_state_bridge_get_real_pipeline_layout(state, args->layout);

    if (real_cmd == VK_NULL_HANDLE || real_layout == VK_NULL_HANDLE) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    const uint8_t* data_bytes = (const uint8_t*)args->pData;
    bool success = true;
    const uint32_t write_count = tmpl_info.entry_count;
    VkWriteDescriptorSet* writes =
        write_count ? calloc(write_count, sizeof(*writes)) : NULL;
    VkDescriptorBufferInfo** buffer_arrays =
        write_count ? calloc(write_count, sizeof(*buffer_arrays)) : NULL;
    VkDescriptorImageInfo** image_arrays =
        write_count ? calloc(write_count, sizeof(*image_arrays)) : NULL;
    VkBufferView** texel_arrays =
        write_count ? calloc(write_count, sizeof(*texel_arrays)) : NULL;

    if ((write_count && (!writes || !buffer_arrays || !image_arrays || !texel_arrays))) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for push descriptor template");
        success = false;
        goto cleanup;
    }

    for (uint32_t i = 0; i < write_count; ++i) {
        const VkDescriptorUpdateTemplateEntry* entry = &tmpl_info.entries[i];
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = entry->dstBinding;
        write.dstArrayElement = entry->dstArrayElement;
        write.descriptorCount = entry->descriptorCount;
        write.descriptorType = entry->descriptorType;
        write.dstSet = VK_NULL_HANDLE;

        switch (entry->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            buffer_arrays[i] =
                calloc(entry->descriptorCount ? entry->descriptorCount : 1, sizeof(VkDescriptorBufferInfo));
            if (!buffer_arrays[i]) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in push template buffer translation");
                success = false;
                goto cleanup;
            }
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                size_t offset = (size_t)entry->offset + (size_t)entry->stride * j;
                const VkDescriptorBufferInfo* src =
                    data_bytes ? (const VkDescriptorBufferInfo*)(data_bytes + offset) : NULL;
                if (!src) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing buffer info in push template");
                    success = false;
                    goto cleanup;
                }
                buffer_arrays[i][j] = *src;
                buffer_arrays[i][j].buffer =
                    server_state_bridge_get_real_buffer(state, src->buffer);
                if (buffer_arrays[i][j].buffer == VK_NULL_HANDLE && src->buffer != VK_NULL_HANDLE) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown buffer in push template");
                    success = false;
                    goto cleanup;
                }
            }
            write.pBufferInfo = buffer_arrays[i];
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            image_arrays[i] =
                calloc(entry->descriptorCount ? entry->descriptorCount : 1, sizeof(VkDescriptorImageInfo));
            if (!image_arrays[i]) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in push template image translation");
                success = false;
                goto cleanup;
            }
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                size_t offset = (size_t)entry->offset + (size_t)entry->stride * j;
                const VkDescriptorImageInfo* src =
                    data_bytes ? (const VkDescriptorImageInfo*)(data_bytes + offset) : NULL;
                if (!src) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing image info in push template");
                    success = false;
                    goto cleanup;
                }
                image_arrays[i][j] = *src;
                if (image_arrays[i][j].imageView != VK_NULL_HANDLE) {
                    image_arrays[i][j].imageView =
                        server_state_bridge_get_real_image_view(state, src->imageView);
                    if (image_arrays[i][j].imageView == VK_NULL_HANDLE) {
                        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown image view in push template");
                        success = false;
                        goto cleanup;
                    }
                }
                if (image_arrays[i][j].sampler != VK_NULL_HANDLE) {
                    image_arrays[i][j].sampler =
                        server_state_bridge_get_real_sampler(state, src->sampler);
                    if (image_arrays[i][j].sampler == VK_NULL_HANDLE) {
                        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown sampler in push template");
                        success = false;
                        goto cleanup;
                    }
                }
            }
            write.pImageInfo = image_arrays[i];
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            texel_arrays[i] =
                calloc(entry->descriptorCount ? entry->descriptorCount : 1, sizeof(VkBufferView));
            if (!texel_arrays[i]) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in push template texel translation");
                success = false;
                goto cleanup;
            }
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                size_t offset = (size_t)entry->offset + (size_t)entry->stride * j;
                const VkBufferView* src =
                    data_bytes ? (const VkBufferView*)(data_bytes + offset) : NULL;
                if (!src) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing texel buffer view in push template");
                    success = false;
                    goto cleanup;
                }
                texel_arrays[i][j] =
                    server_state_bridge_get_real_buffer_view(state, *src);
                if (texel_arrays[i][j] == VK_NULL_HANDLE && *src != VK_NULL_HANDLE) {
                    VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown buffer view in push template");
                    success = false;
                    goto cleanup;
                }
            }
            write.pTexelBufferView = texel_arrays[i];
            break;
        default:
            VP_LOG_ERROR(SERVER,
                         "[Venus Server]   -> ERROR: Unsupported descriptor type %d in push template",
                         entry->descriptorType);
            success = false;
            goto cleanup;
        }

        writes[i] = write;
    }

    vkCmdPushDescriptorSet(real_cmd,
                           tmpl_info.bind_point,
                           real_layout,
                           args->set,
                           write_count,
                           writes);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Push descriptors recorded via template");

cleanup:
    if (buffer_arrays) {
        for (uint32_t i = 0; i < write_count; ++i) {
            free(buffer_arrays[i]);
        }
    }
    if (image_arrays) {
        for (uint32_t i = 0; i < write_count; ++i) {
            free(image_arrays[i]);
        }
    }
    if (texel_arrays) {
        for (uint32_t i = 0; i < write_count; ++i) {
            free(texel_arrays[i]);
        }
    }
    free(writes);
    free(buffer_arrays);
    free(image_arrays);
    free(texel_arrays);

    if (!success) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
    }
    free(tmpl_info.entries);
}

static void server_dispatch_vkCmdPushDescriptorSet2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdPushDescriptorSet2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushDescriptorSet2");
    if (!args->pPushDescriptorSetInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing push descriptor info");
        return;
    }

    VkPipelineBindPoint bind_point =
        infer_bind_point_from_stages(args->pPushDescriptorSetInfo->stageFlags);

    struct vn_command_vkCmdPushDescriptorSet compat = {
        .commandBuffer = args->commandBuffer,
        .pipelineBindPoint = bind_point,
        .layout = args->pPushDescriptorSetInfo->layout,
        .set = args->pPushDescriptorSetInfo->set,
        .descriptorWriteCount = args->pPushDescriptorSetInfo->descriptorWriteCount,
        .pDescriptorWrites = args->pPushDescriptorSetInfo->pDescriptorWrites,
    };
    server_dispatch_vkCmdPushDescriptorSet(ctx, &compat);
}

static void server_dispatch_vkCmdPushDescriptorSetWithTemplate2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdPushDescriptorSetWithTemplate2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushDescriptorSetWithTemplate2");
    if (!args->pPushDescriptorSetWithTemplateInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing push descriptor template info");
        return;
    }

    struct vn_command_vkCmdPushDescriptorSetWithTemplate compat = {
        .commandBuffer = args->commandBuffer,
        .descriptorUpdateTemplate =
            args->pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate,
        .layout = args->pPushDescriptorSetWithTemplateInfo->layout,
        .set = args->pPushDescriptorSetWithTemplateInfo->set,
        .pData = args->pPushDescriptorSetWithTemplateInfo->pData,
    };
    server_dispatch_vkCmdPushDescriptorSetWithTemplate(ctx, &compat);
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

static void server_dispatch_vkCreatePipelineCache(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCreatePipelineCache* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreatePipelineCache");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_ERROR_INITIALIZATION_FAILED;
    if (!args->pPipelineCache || !args->pCreateInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing output pointer or create info");
        return;
    }
    VkPipelineCache cache =
        server_state_bridge_create_pipeline_cache(state, args->device, args->pCreateInfo);
    if (cache == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create pipeline cache");
        return;
    }
    *args->pPipelineCache = cache;
    args->ret = VK_SUCCESS;
}

static void server_dispatch_vkDestroyPipelineCache(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkDestroyPipelineCache* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyPipelineCache");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->pipelineCache != VK_NULL_HANDLE) {
        server_state_bridge_destroy_pipeline_cache(state, args->pipelineCache);
    }
}

static void server_dispatch_vkGetPipelineCacheData(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkGetPipelineCacheData* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetPipelineCacheData");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_get_pipeline_cache_data(state,
                                                            args->device,
                                                            args->pipelineCache,
                                                            args->pDataSize,
                                                            args->pData);
}

static void server_dispatch_vkMergePipelineCaches(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkMergePipelineCaches* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkMergePipelineCaches");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_merge_pipeline_caches(state,
                                                          args->device,
                                                          args->dstCache,
                                                          args->srcCacheCount,
                                                          args->pSrcCaches);
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

static void server_dispatch_vkCreateQueryPool(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCreateQueryPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateQueryPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_ERROR_INITIALIZATION_FAILED;
    if (!args->pCreateInfo || !args->pQueryPool) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing create info or output pointer");
        return;
    }
    VkQueryPool pool =
        server_state_bridge_create_query_pool(state, args->device, args->pCreateInfo);
    if (pool == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Failed to create query pool");
        return;
    }
    *args->pQueryPool = pool;
    args->ret = VK_SUCCESS;
}

static void server_dispatch_vkDestroyQueryPool(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkDestroyQueryPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyQueryPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->queryPool != VK_NULL_HANDLE) {
        server_state_bridge_destroy_query_pool(state, args->queryPool);
    }
}

static void server_dispatch_vkResetQueryPool(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkResetQueryPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetQueryPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    VkDevice real_device =
        server_state_bridge_get_query_pool_real_device(state, args->queryPool);
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_device == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown query pool");
        return;
    }
    vkResetQueryPool(real_device, real_pool, args->firstQuery, args->queryCount);
}

static void server_dispatch_vkGetQueryPoolResults(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkGetQueryPoolResults* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetQueryPoolResults");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_get_query_pool_results(state,
                                                           args->device,
                                                           args->queryPool,
                                                           args->firstQuery,
                                                           args->queryCount,
                                                           args->dataSize,
                                                           args->pData,
                                                           args->stride,
                                                           args->flags);
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

static void server_dispatch_vkGetRenderAreaGranularity(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkGetRenderAreaGranularity* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetRenderAreaGranularity");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pGranularity) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pGranularity is NULL");
        return;
    }
    server_state_bridge_get_render_area_granularity(state,
                                                    args->device,
                                                    args->renderPass,
                                                    args->pGranularity);
    VP_LOG_INFO(SERVER,
                "[Venus Server]   -> Granularity %ux%u",
                args->pGranularity->width,
                args->pGranularity->height);
}

static void server_dispatch_vkGetRenderingAreaGranularity(struct vn_dispatch_context* ctx,
                                                          struct vn_command_vkGetRenderingAreaGranularity* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetRenderingAreaGranularity");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pRenderingAreaInfo || !args->pGranularity) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: missing rendering area info or granularity");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device in vkGetRenderingAreaGranularity");
        return;
    }

    vkGetRenderingAreaGranularity(real_device, args->pRenderingAreaInfo, args->pGranularity);
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

static void server_dispatch_vkBindImageMemory2(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkBindImageMemory2* args) {
    VP_LOG_INFO(SERVER,
                "[Venus Server] Dispatching vkBindImageMemory2 (count=%u)",
                args->bindInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_bind_image_memory2(state,
                                                       args->device,
                                                       args->bindInfoCount,
                                                       args->pBindInfos);
    if (args->ret == VK_SUCCESS) {
        VP_LOG_INFO(SERVER, "[Venus Server]   -> Bound %u image(s)", args->bindInfoCount);
    } else {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkBindImageMemory2 failed: %d", args->ret);
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

static void server_dispatch_vkGetImageSubresourceLayout2(struct vn_dispatch_context* ctx,
                                                         struct vn_command_vkGetImageSubresourceLayout2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetImageSubresourceLayout2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pLayout || !args->pSubresource) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: pLayout or pSubresource is NULL");
        return;
    }

    VkSubresourceLayout base_layout = {};
    if (!server_state_bridge_get_image_subresource_layout(state,
                                                          args->image,
                                                          &args->pSubresource->imageSubresource,
                                                          &base_layout)) {
        memset(&args->pLayout->subresourceLayout, 0, sizeof(args->pLayout->subresourceLayout));
        VP_LOG_WARN(SERVER, "[Venus Server]   -> Warning: Image not found or invalid subresource");
        return;
    }

    args->pLayout->subresourceLayout = base_layout;
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Returned subresource layout2 (offset=%llu)",
           (unsigned long long)args->pLayout->subresourceLayout.offset);
}

static void server_dispatch_vkGetDeviceImageSubresourceLayout(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetDeviceImageSubresourceLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetDeviceImageSubresourceLayout");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pInfo || !args->pLayout || !args->pInfo->pCreateInfo || !args->pInfo->pSubresource) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkGetDeviceImageSubresourceLayout");
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        return;
    }

    vkGetDeviceImageSubresourceLayout(real_device, args->pInfo, args->pLayout);
}

static void server_dispatch_vkCopyMemoryToImage(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCopyMemoryToImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCopyMemoryToImage (unsupported)");
    args->ret = VK_ERROR_FEATURE_NOT_PRESENT;
}

static void server_dispatch_vkCopyImageToMemory(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCopyImageToMemory* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCopyImageToMemory (unsupported)");
    args->ret = VK_ERROR_FEATURE_NOT_PRESENT;
}

static void server_dispatch_vkCopyImageToImage(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCopyImageToImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCopyImageToImage");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCopyImageToImageInfo || !args->pCopyImageToImageInfo->pRegions ||
        args->pCopyImageToImageInfo->regionCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCopyImageToImage");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkCopyImageToImageInfo info = *args->pCopyImageToImageInfo;
    info.srcImage = server_state_bridge_get_real_image(state, info.srcImage);
    info.dstImage = server_state_bridge_get_real_image(state, info.dstImage);
    if (info.srcImage == VK_NULL_HANDLE || info.dstImage == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown images in vkCopyImageToImage");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    PFN_vkCopyImageToImage fp =
        (PFN_vkCopyImageToImage)vkGetDeviceProcAddr(real_device, "vkCopyImageToImage");
    if (!fp) {
        fp = (PFN_vkCopyImageToImage)vkGetDeviceProcAddr(real_device, "vkCopyImageToImageEXT");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCopyImageToImage not supported on device");
        args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
        return;
    }

    args->ret = fp(real_device, &info);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkCopyImageToImage returned %d", args->ret);
    }
}

static void server_dispatch_vkTransitionImageLayout(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkTransitionImageLayout* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkTransitionImageLayout");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (args->transitionCount == 0 || !args->pTransitions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing transitions");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkHostImageLayoutTransitionInfo* transitions =
        calloc(args->transitionCount, sizeof(VkHostImageLayoutTransitionInfo));
    if (!transitions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for transitions");
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }

    for (uint32_t i = 0; i < args->transitionCount; ++i) {
        transitions[i] = args->pTransitions[i];
        transitions[i].image =
            server_state_bridge_get_real_image(state, args->pTransitions[i].image);
        if (transitions[i].image == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown image in transition %u", i);
            free(transitions);
            args->ret = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }

    PFN_vkTransitionImageLayout fp =
        (PFN_vkTransitionImageLayout)vkGetDeviceProcAddr(real_device, "vkTransitionImageLayout");
    if (!fp) {
        fp = (PFN_vkTransitionImageLayout)vkGetDeviceProcAddr(real_device, "vkTransitionImageLayoutEXT");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkTransitionImageLayout not supported on device");
        free(transitions);
        args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
        return;
    }

    args->ret = fp(real_device, args->transitionCount, transitions);
    free(transitions);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkTransitionImageLayout returned %d", args->ret);
    }
}

static void server_dispatch_vkCopyImageToMemoryMESA(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCopyImageToMemoryMESA* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCopyImageToMemoryMESA");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCopyImageToMemoryInfo || !args->pData) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing copy info or data buffer");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkImage real_image = server_state_bridge_get_real_image(state, args->pCopyImageToMemoryInfo->srcImage);
    if (real_image == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown image in vkCopyImageToMemoryMESA");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    if (args->dataSize == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: dataSize is zero");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    void* region_data = malloc(args->dataSize);
    if (!region_data) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory");
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }

    VkImageToMemoryCopy region = {};
    region.sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY;
    region.pNext = args->pCopyImageToMemoryInfo->pNext;
    region.pHostPointer = region_data;
    region.memoryRowLength = args->pCopyImageToMemoryInfo->memoryRowLength;
    region.memoryImageHeight = args->pCopyImageToMemoryInfo->memoryImageHeight;
    region.imageSubresource = args->pCopyImageToMemoryInfo->imageSubresource;
    region.imageOffset = args->pCopyImageToMemoryInfo->imageOffset;
    region.imageExtent = args->pCopyImageToMemoryInfo->imageExtent;

    VkCopyImageToMemoryInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO;
    info.pNext = args->pCopyImageToMemoryInfo->pNext;
    info.flags = args->pCopyImageToMemoryInfo->flags;
    info.srcImage = real_image;
    info.srcImageLayout = args->pCopyImageToMemoryInfo->srcImageLayout;
    info.regionCount = 1;
    info.pRegions = &region;

    PFN_vkCopyImageToMemory fp =
        (PFN_vkCopyImageToMemory)vkGetDeviceProcAddr(real_device, "vkCopyImageToMemory");
    if (!fp) {
        fp = (PFN_vkCopyImageToMemory)vkGetDeviceProcAddr(real_device, "vkCopyImageToMemoryEXT");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCopyImageToMemory not supported on device");
        free(region_data);
        args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
        return;
    }

    args->ret = fp(real_device, &info);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkCopyImageToMemory returned %d", args->ret);
        free(region_data);
        return;
    }

    memcpy(args->pData, region_data, args->dataSize);
    free(region_data);
}

static void server_dispatch_vkCopyMemoryToImageMESA(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCopyMemoryToImageMESA* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCopyMemoryToImageMESA");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (!args->pCopyMemoryToImageInfo || !args->pCopyMemoryToImageInfo->pRegions ||
        args->pCopyMemoryToImageInfo->regionCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid copy info");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkDevice real_device = server_state_bridge_get_real_device(state, args->device);
    if (real_device == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown device");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkImage real_image = server_state_bridge_get_real_image(state, args->pCopyMemoryToImageInfo->dstImage);
    if (real_image == VK_NULL_HANDLE) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown image in vkCopyMemoryToImageMESA");
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkMemoryToImageCopy* regions = calloc(args->pCopyMemoryToImageInfo->regionCount,
                                          sizeof(VkMemoryToImageCopy));
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for regions");
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    for (uint32_t i = 0; i < args->pCopyMemoryToImageInfo->regionCount; ++i) {
        const VkMemoryToImageCopyMESA* mesa_region = &args->pCopyMemoryToImageInfo->pRegions[i];
        if (!mesa_region->pData && mesa_region->dataSize > 0) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Region %u missing data pointer", i);
            free(regions);
            args->ret = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }

        VkMemoryToImageCopy region = {};
        region.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY;
        region.pNext = mesa_region->pNext;
        region.pHostPointer = mesa_region->pData;
        region.memoryRowLength = mesa_region->memoryRowLength;
        region.memoryImageHeight = mesa_region->memoryImageHeight;
        region.imageSubresource = mesa_region->imageSubresource;
        region.imageOffset = mesa_region->imageOffset;
        region.imageExtent = mesa_region->imageExtent;
        regions[i] = region;
    }

    VkCopyMemoryToImageInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO;
    info.pNext = args->pCopyMemoryToImageInfo->pNext;
    info.flags = args->pCopyMemoryToImageInfo->flags;
    info.dstImage = real_image;
    info.dstImageLayout = args->pCopyMemoryToImageInfo->dstImageLayout;
    info.regionCount = args->pCopyMemoryToImageInfo->regionCount;
    info.pRegions = regions;

    PFN_vkCopyMemoryToImage fp =
        (PFN_vkCopyMemoryToImage)vkGetDeviceProcAddr(real_device, "vkCopyMemoryToImage");
    if (!fp) {
        fp = (PFN_vkCopyMemoryToImage)vkGetDeviceProcAddr(real_device, "vkCopyMemoryToImageEXT");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCopyMemoryToImage not supported on device");
        free(regions);
        args->ret = VK_ERROR_EXTENSION_NOT_PRESENT;
        return;
    }

    args->ret = fp(real_device, &info);
    free(regions);
    if (args->ret != VK_SUCCESS) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> vkCopyMemoryToImage returned %d", args->ret);
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

static void server_dispatch_vkTrimCommandPool(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkTrimCommandPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkTrimCommandPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    server_state_bridge_trim_command_pool(state, args->device, args->commandPool, args->flags);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> Command pool trimmed");
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

static void server_dispatch_vkCmdCopyBuffer2(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdCopyBuffer2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyBuffer2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyBuffer2")) {
        return;
    }
    if (!args->pCopyBufferInfo || args->pCopyBufferInfo->regionCount == 0 ||
        !args->pCopyBufferInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdCopyBuffer2");
        return;
    }

    VkBufferCopy* regions = clone_buffer_copy2_array(args->pCopyBufferInfo->regionCount,
                                                     args->pCopyBufferInfo->pRegions);
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdCopyBuffer2");
        return;
    }
    bool valid = server_state_bridge_validate_cmd_copy_buffer(state,
                                                              args->pCopyBufferInfo->srcBuffer,
                                                              args->pCopyBufferInfo->dstBuffer,
                                                              args->pCopyBufferInfo->regionCount,
                                                              regions);
    free(regions);
    if (!valid) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyBuffer2");
    VkBuffer real_src = get_real_buffer(state, args->pCopyBufferInfo->srcBuffer, "vkCmdCopyBuffer2");
    VkBuffer real_dst = get_real_buffer(state, args->pCopyBufferInfo->dstBuffer, "vkCmdCopyBuffer2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkCopyBufferInfo2 info = *args->pCopyBufferInfo;
    info.srcBuffer = real_src;
    info.dstBuffer = real_dst;
    vkCmdCopyBuffer2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyBuffer2 recorded");
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

static void server_dispatch_vkCmdCopyImage2(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdCopyImage2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyImage2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyImage2")) {
        return;
    }
    if (!args->pCopyImageInfo || args->pCopyImageInfo->regionCount == 0 ||
        !args->pCopyImageInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdCopyImage2");
        return;
    }

    VkImageCopy* regions = clone_image_copy2_array(args->pCopyImageInfo->regionCount,
                                                   args->pCopyImageInfo->pRegions);
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdCopyImage2");
        return;
    }
    bool valid = server_state_bridge_validate_cmd_copy_image(state,
                                                             args->pCopyImageInfo->srcImage,
                                                             args->pCopyImageInfo->dstImage,
                                                             args->pCopyImageInfo->regionCount,
                                                             regions);
    free(regions);
    if (!valid) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyImage2");
    VkImage real_src = get_real_image(state, args->pCopyImageInfo->srcImage, "vkCmdCopyImage2");
    VkImage real_dst = get_real_image(state, args->pCopyImageInfo->dstImage, "vkCmdCopyImage2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkCopyImageInfo2 info = *args->pCopyImageInfo;
    info.srcImage = real_src;
    info.dstImage = real_dst;
    vkCmdCopyImage2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyImage2 recorded");
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

static void server_dispatch_vkCmdBlitImage2(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdBlitImage2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBlitImage2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBlitImage2")) {
        return;
    }
    if (!args->pBlitImageInfo || args->pBlitImageInfo->regionCount == 0 ||
        !args->pBlitImageInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdBlitImage2");
        return;
    }

    VkImageBlit* regions = clone_image_blit2_array(args->pBlitImageInfo->regionCount,
                                                   args->pBlitImageInfo->pRegions);
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdBlitImage2");
        return;
    }
    bool valid = server_state_bridge_validate_cmd_blit_image(state,
                                                             args->pBlitImageInfo->srcImage,
                                                             args->pBlitImageInfo->dstImage,
                                                             args->pBlitImageInfo->regionCount,
                                                             regions);
    free(regions);
    if (!valid) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBlitImage2");
    VkImage real_src = get_real_image(state, args->pBlitImageInfo->srcImage, "vkCmdBlitImage2");
    VkImage real_dst = get_real_image(state, args->pBlitImageInfo->dstImage, "vkCmdBlitImage2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkBlitImageInfo2 info = *args->pBlitImageInfo;
    info.srcImage = real_src;
    info.dstImage = real_dst;
    vkCmdBlitImage2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBlitImage2 recorded");
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

static void server_dispatch_vkCmdCopyBufferToImage2(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdCopyBufferToImage2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyBufferToImage2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyBufferToImage2")) {
        return;
    }
    if (!args->pCopyBufferToImageInfo || args->pCopyBufferToImageInfo->regionCount == 0 ||
        !args->pCopyBufferToImageInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdCopyBufferToImage2");
        return;
    }

    VkBufferImageCopy* regions = clone_buffer_image_copy2_array(args->pCopyBufferToImageInfo->regionCount,
                                                                args->pCopyBufferToImageInfo->pRegions);
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdCopyBufferToImage2");
        return;
    }
    bool valid = server_state_bridge_validate_cmd_copy_buffer_to_image(state,
                                                                       args->pCopyBufferToImageInfo->srcBuffer,
                                                                       args->pCopyBufferToImageInfo->dstImage,
                                                                       args->pCopyBufferToImageInfo->regionCount,
                                                                       regions);
    free(regions);
    if (!valid) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyBufferToImage2");
    VkBuffer real_src = get_real_buffer(state, args->pCopyBufferToImageInfo->srcBuffer, "vkCmdCopyBufferToImage2");
    VkImage real_dst = get_real_image(state, args->pCopyBufferToImageInfo->dstImage, "vkCmdCopyBufferToImage2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkCopyBufferToImageInfo2 info = *args->pCopyBufferToImageInfo;
    info.srcBuffer = real_src;
    info.dstImage = real_dst;
    vkCmdCopyBufferToImage2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyBufferToImage2 recorded");
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

static void server_dispatch_vkCmdCopyImageToBuffer2(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdCopyImageToBuffer2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyImageToBuffer2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyImageToBuffer2")) {
        return;
    }
    if (!args->pCopyImageToBufferInfo || args->pCopyImageToBufferInfo->regionCount == 0 ||
        !args->pCopyImageToBufferInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdCopyImageToBuffer2");
        return;
    }

    VkBufferImageCopy* regions = clone_buffer_image_copy2_array(args->pCopyImageToBufferInfo->regionCount,
                                                                args->pCopyImageToBufferInfo->pRegions);
    if (!regions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdCopyImageToBuffer2");
        return;
    }
    bool valid = server_state_bridge_validate_cmd_copy_image_to_buffer(state,
                                                                       args->pCopyImageToBufferInfo->srcImage,
                                                                       args->pCopyImageToBufferInfo->dstBuffer,
                                                                       args->pCopyImageToBufferInfo->regionCount,
                                                                       regions);
    free(regions);
    if (!valid) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyImageToBuffer2");
    VkImage real_src = get_real_image(state, args->pCopyImageToBufferInfo->srcImage, "vkCmdCopyImageToBuffer2");
    VkBuffer real_dst = get_real_buffer(state, args->pCopyImageToBufferInfo->dstBuffer, "vkCmdCopyImageToBuffer2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkCopyImageToBufferInfo2 info = *args->pCopyImageToBufferInfo;
    info.srcImage = real_src;
    info.dstBuffer = real_dst;
    vkCmdCopyImageToBuffer2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdCopyImageToBuffer2 recorded");
}

static void server_dispatch_vkCmdResolveImage(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdResolveImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdResolveImage");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdResolveImage")) {
        return;
    }
    if (args->regionCount == 0 || !args->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid regions for vkCmdResolveImage");
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdResolveImage");
    VkImage real_src = get_real_image(state, args->srcImage, "vkCmdResolveImage");
    VkImage real_dst = get_real_image(state, args->dstImage, "vkCmdResolveImage");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    vkCmdResolveImage(real_cb,
                      real_src,
                      args->srcImageLayout,
                      real_dst,
                      args->dstImageLayout,
                      args->regionCount,
                      args->pRegions);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdResolveImage recorded");
}

static void server_dispatch_vkCmdResolveImage2(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCmdResolveImage2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdResolveImage2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdResolveImage2")) {
        return;
    }
    if (!args->pResolveImageInfo || args->pResolveImageInfo->regionCount == 0 ||
        !args->pResolveImageInfo->pRegions) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdResolveImage2");
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdResolveImage2");
    VkImage real_src = get_real_image(state, args->pResolveImageInfo->srcImage, "vkCmdResolveImage2");
    VkImage real_dst = get_real_image(state, args->pResolveImageInfo->dstImage, "vkCmdResolveImage2");
    if (real_cb == VK_NULL_HANDLE || real_src == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return;
    }

    VkResolveImageInfo2 info = *args->pResolveImageInfo;
    info.srcImage = real_src;
    info.dstImage = real_dst;
    vkCmdResolveImage2(real_cb, &info);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdResolveImage2 recorded");
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

static void server_dispatch_vkCmdClearDepthStencilImage(struct vn_dispatch_context* ctx,
                                                        struct vn_command_vkCmdClearDepthStencilImage* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdClearDepthStencilImage (ranges=%u)", args->rangeCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdClearDepthStencilImage")) {
        return;
    }
    if (args->rangeCount == 0 || !args->pRanges || !args->pDepthStencil) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdClearDepthStencilImage");
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdClearDepthStencilImage");
    VkImage real_image = get_real_image(state, args->image, "vkCmdClearDepthStencilImage");
    if (real_cb == VK_NULL_HANDLE || real_image == VK_NULL_HANDLE) {
        return;
    }
    vkCmdClearDepthStencilImage(real_cb,
                                real_image,
                                args->imageLayout,
                                args->pDepthStencil,
                                args->rangeCount,
                                args->pRanges);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdClearDepthStencilImage recorded");
}

static void server_dispatch_vkCmdClearAttachments(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCmdClearAttachments* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdClearAttachments (attachments=%u, rects=%u)",
           args->attachmentCount,
           args->rectCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdClearAttachments")) {
        return;
    }
    if (args->attachmentCount == 0 || args->rectCount == 0 || !args->pAttachments || !args->pRects) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdClearAttachments");
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdClearAttachments");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    vkCmdClearAttachments(real_cb,
                          args->attachmentCount,
                          args->pAttachments,
                          args->rectCount,
                          args->pRects);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdClearAttachments recorded");
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

static void server_dispatch_vkCmdBeginRenderPass2(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCmdBeginRenderPass2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBeginRenderPass2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBeginRenderPass2")) {
        return;
    }
    if (!args->pRenderPassBegin || !args->pSubpassBeginInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing begin info for vkCmdBeginRenderPass2");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBeginRenderPass2");
    VkRenderPass real_rp =
        server_state_bridge_get_real_render_pass(state, args->pRenderPassBegin->renderPass);
    VkFramebuffer real_fb =
        server_state_bridge_get_real_framebuffer(state, args->pRenderPassBegin->framebuffer);
    if (real_cb == VK_NULL_HANDLE || real_rp == VK_NULL_HANDLE || real_fb == VK_NULL_HANDLE) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkRenderPassBeginInfo begin_info = *args->pRenderPassBegin;
    begin_info.renderPass = real_rp;
    begin_info.framebuffer = real_fb;
    vkCmdBeginRenderPass2(real_cb, &begin_info, args->pSubpassBeginInfo);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBeginRenderPass2 recorded");
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

static void server_dispatch_vkCmdNextSubpass(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdNextSubpass* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdNextSubpass");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdNextSubpass")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdNextSubpass");
    if (!real_cb) {
        return;
    }
    vkCmdNextSubpass(real_cb, args->contents);
}

static void server_dispatch_vkCmdNextSubpass2(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdNextSubpass2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdNextSubpass2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdNextSubpass2")) {
        return;
    }
    if (!args->pSubpassBeginInfo || !args->pSubpassEndInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing subpass info for vkCmdNextSubpass2");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdNextSubpass2");
    if (!real_cb) {
        return;
    }
    vkCmdNextSubpass2(real_cb, args->pSubpassBeginInfo, args->pSubpassEndInfo);
}

static void server_dispatch_vkCmdEndRenderPass2(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdEndRenderPass2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdEndRenderPass2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdEndRenderPass2")) {
        return;
    }
    if (!args->pSubpassEndInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing end info for vkCmdEndRenderPass2");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdEndRenderPass2");
    if (!real_cb) {
        return;
    }
    vkCmdEndRenderPass2(real_cb, args->pSubpassEndInfo);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdEndRenderPass2 recorded");
}

static void server_dispatch_vkCmdBeginRendering(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdBeginRendering* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBeginRendering");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBeginRendering")) {
        return;
    }
    if (!args->pRenderingInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing rendering info");
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBeginRendering");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }

    VkRenderingInfo info = *args->pRenderingInfo;
    VkRenderingAttachmentInfo* color_attachments = NULL;
    if (info.colorAttachmentCount > 0) {
        if (!info.pColorAttachments) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing color attachments");
            return;
        }
        color_attachments = calloc(info.colorAttachmentCount, sizeof(*color_attachments));
        if (!color_attachments) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdBeginRendering");
            return;
        }
        memcpy(color_attachments,
               info.pColorAttachments,
               info.colorAttachmentCount * sizeof(*color_attachments));
        for (uint32_t i = 0; i < info.colorAttachmentCount; ++i) {
            if (!translate_rendering_attachment(state, &color_attachments[i], "vkCmdBeginRendering")) {
                free(color_attachments);
                return;
            }
        }
        info.pColorAttachments = color_attachments;
    } else {
        info.pColorAttachments = NULL;
    }

    VkRenderingAttachmentInfo depth_attachment;
    if (info.pDepthAttachment) {
        depth_attachment = *info.pDepthAttachment;
        if (!translate_rendering_attachment(state, &depth_attachment, "vkCmdBeginRendering")) {
            free(color_attachments);
            return;
        }
        info.pDepthAttachment = &depth_attachment;
    }

    VkRenderingAttachmentInfo stencil_attachment;
    if (info.pStencilAttachment) {
        stencil_attachment = *info.pStencilAttachment;
        if (!translate_rendering_attachment(state, &stencil_attachment, "vkCmdBeginRendering")) {
            free(color_attachments);
            return;
        }
        info.pStencilAttachment = &stencil_attachment;
    }

    vkCmdBeginRendering(real_cb, &info);
    free(color_attachments);
}

static void server_dispatch_vkCmdEndRendering(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdEndRendering* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdEndRendering");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdEndRendering")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdEndRendering");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndRendering(real_cb);
}

static void server_dispatch_vkCmdSetRenderingAttachmentLocations(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdSetRenderingAttachmentLocations* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetRenderingAttachmentLocations");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetRenderingAttachmentLocations")) {
        return;
    }
    if (!args->pLocationInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing location info");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    if (args->pLocationInfo->colorAttachmentCount > 0 &&
        !args->pLocationInfo->pColorAttachmentLocations) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: colorAttachmentCount set without locations");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetRenderingAttachmentLocations");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    VkDevice real_device = server_state_bridge_get_command_buffer_real_device(state, args->commandBuffer);
    PFN_vkCmdSetRenderingAttachmentLocations fp =
        (PFN_vkCmdSetRenderingAttachmentLocations)vkGetDeviceProcAddr(real_device, "vkCmdSetRenderingAttachmentLocations");
    if (!fp) {
        fp = (PFN_vkCmdSetRenderingAttachmentLocations)vkGetDeviceProcAddr(real_device, "vkCmdSetRenderingAttachmentLocationsKHR");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCmdSetRenderingAttachmentLocations not supported on device");
        return;
    }
    fp(real_cb, args->pLocationInfo);
}

static void server_dispatch_vkCmdSetRenderingInputAttachmentIndices(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdSetRenderingInputAttachmentIndices* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetRenderingInputAttachmentIndices");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetRenderingInputAttachmentIndices")) {
        return;
    }
    if (!args->pInputAttachmentIndexInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing input attachment indices");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    if (args->pInputAttachmentIndexInfo->colorAttachmentCount > 0 &&
        !args->pInputAttachmentIndexInfo->pColorAttachmentInputIndices) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: colorAttachmentCount set without indices");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetRenderingInputAttachmentIndices");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    vkCmdSetRenderingInputAttachmentIndices(real_cb, args->pInputAttachmentIndexInfo);
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

static void server_dispatch_vkCmdBindIndexBuffer(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdBindIndexBuffer* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindIndexBuffer");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindIndexBuffer")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBindIndexBuffer");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdBindIndexBuffer");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBindIndexBuffer(real_cb, real_buffer, args->offset, args->indexType);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBindIndexBuffer recorded");
}

static void server_dispatch_vkCmdBindIndexBuffer2(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCmdBindIndexBuffer2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindIndexBuffer2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindIndexBuffer2")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBindIndexBuffer2");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdBindIndexBuffer2");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }

    VkDevice real_device = server_state_bridge_get_command_buffer_real_device(state, args->commandBuffer);
    PFN_vkCmdBindIndexBuffer2 fp =
        (PFN_vkCmdBindIndexBuffer2)vkGetDeviceProcAddr(real_device, "vkCmdBindIndexBuffer2");
    if (!fp) {
        fp = (PFN_vkCmdBindIndexBuffer2)vkGetDeviceProcAddr(real_device, "vkCmdBindIndexBuffer2KHR");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCmdBindIndexBuffer2 not supported on device");
        return;
    }

    fp(real_cb, real_buffer, args->offset, args->size, args->indexType);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBindIndexBuffer2 recorded");
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

static void server_dispatch_vkCmdBindVertexBuffers2(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdBindVertexBuffers2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindVertexBuffers2 (count=%u)",
           args->bindingCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBindVertexBuffers2")) {
        return;
    }
    if (args->bindingCount == 0 || !args->pBuffers || !args->pOffsets) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdBindVertexBuffers2");
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBindVertexBuffers2");
    if (!real_cb) {
        return;
    }
    VkBuffer* real_buffers = calloc(args->bindingCount, sizeof(*real_buffers));
    if (!real_buffers) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for vertex buffers");
        return;
    }
    for (uint32_t i = 0; i < args->bindingCount; ++i) {
        if (args->pBuffers[i] == VK_NULL_HANDLE) {
            real_buffers[i] = VK_NULL_HANDLE;
            continue;
        }
        real_buffers[i] =
            get_real_buffer(state, args->pBuffers[i], "vkCmdBindVertexBuffers2");
        if (real_buffers[i] == VK_NULL_HANDLE) {
            free(real_buffers);
            return;
        }
    }

    vkCmdBindVertexBuffers2(real_cb,
                            args->firstBinding,
                            args->bindingCount,
                            real_buffers,
                            args->pOffsets,
                            args->pSizes,
                            args->pStrides);
    free(real_buffers);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdBindVertexBuffers2 recorded");
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

static void server_dispatch_vkCmdBindDescriptorSets2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdBindDescriptorSets2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBindDescriptorSets2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!args->pBindDescriptorSetsInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing bind descriptor info");
        return;
    }

    VkPipelineBindPoint bind_point =
        infer_bind_point_from_stages(args->pBindDescriptorSetsInfo->stageFlags);

    struct vn_command_vkCmdBindDescriptorSets compat = {
        .commandBuffer = args->commandBuffer,
        .pipelineBindPoint = bind_point,
        .layout = args->pBindDescriptorSetsInfo->layout,
        .firstSet = args->pBindDescriptorSetsInfo->firstSet,
        .descriptorSetCount = args->pBindDescriptorSetsInfo->descriptorSetCount,
        .pDescriptorSets = args->pBindDescriptorSetsInfo->pDescriptorSets,
        .dynamicOffsetCount = args->pBindDescriptorSetsInfo->dynamicOffsetCount,
        .pDynamicOffsets = args->pBindDescriptorSetsInfo->pDynamicOffsets,
    };

    server_dispatch_vkCmdBindDescriptorSets(ctx, &compat);
}

static void server_dispatch_vkCmdPushConstants(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCmdPushConstants* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushConstants");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdPushConstants")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdPushConstants");
    VkPipelineLayout real_layout =
        server_state_bridge_get_real_pipeline_layout(state, args->layout);
    if (real_cb == VK_NULL_HANDLE || real_layout == VK_NULL_HANDLE) {
        return;
    }
    vkCmdPushConstants(real_cb,
                       real_layout,
                       args->stageFlags,
                       args->offset,
                       args->size,
                       args->pValues);
}

static void server_dispatch_vkCmdPushConstants2(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdPushConstants2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPushConstants2");
    if (!args->pPushConstantsInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing push constants info");
        return;
    }
    struct vn_command_vkCmdPushConstants compat = {
        .commandBuffer = args->commandBuffer,
        .layout = args->pPushConstantsInfo->layout,
        .stageFlags = args->pPushConstantsInfo->stageFlags,
        .offset = args->pPushConstantsInfo->offset,
        .size = args->pPushConstantsInfo->size,
        .pValues = args->pPushConstantsInfo->pValues,
    };
    server_dispatch_vkCmdPushConstants(ctx, &compat);
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

static void server_dispatch_vkCmdDispatchIndirect(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCmdDispatchIndirect* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDispatchIndirect");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDispatchIndirect")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDispatchIndirect");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdDispatchIndirect");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDispatchIndirect(real_cb, real_buffer, args->offset);
}

static void server_dispatch_vkCmdDispatchBase(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdDispatchBase* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDispatchBase");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDispatchBase")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDispatchBase");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDispatchBase(real_cb,
                      args->baseGroupX,
                      args->baseGroupY,
                      args->baseGroupZ,
                      args->groupCountX,
                      args->groupCountY,
                      args->groupCountZ);
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

static void server_dispatch_vkCmdSetCullMode(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdSetCullMode* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetCullMode");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetCullMode")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetCullMode");
    if (!real_cb) {
        return;
    }
    vkCmdSetCullMode(real_cb, args->cullMode);
}

static void server_dispatch_vkCmdSetFrontFace(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdSetFrontFace* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetFrontFace");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetFrontFace")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetFrontFace");
    if (!real_cb) {
        return;
    }
    vkCmdSetFrontFace(real_cb, args->frontFace);
}

static void server_dispatch_vkCmdSetPrimitiveTopology(struct vn_dispatch_context* ctx,
                                                      struct vn_command_vkCmdSetPrimitiveTopology* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetPrimitiveTopology");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetPrimitiveTopology")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetPrimitiveTopology");
    if (!real_cb) {
        return;
    }
    vkCmdSetPrimitiveTopology(real_cb, args->primitiveTopology);
}

static void server_dispatch_vkCmdSetBlendConstants(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdSetBlendConstants* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetBlendConstants");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetBlendConstants")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetBlendConstants");
    if (!real_cb) {
        return;
    }
    vkCmdSetBlendConstants(real_cb, args->blendConstants);
}

static void server_dispatch_vkCmdSetLineWidth(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdSetLineWidth* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetLineWidth");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetLineWidth")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetLineWidth");
    if (!real_cb) {
        return;
    }
    vkCmdSetLineWidth(real_cb, args->lineWidth);
}

static void server_dispatch_vkCmdSetLineStipple(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdSetLineStipple* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetLineStipple");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetLineStipple")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetLineStipple");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }
    VkDevice real_device = server_state_bridge_get_command_buffer_real_device(state, args->commandBuffer);
    PFN_vkCmdSetLineStipple fp =
        (PFN_vkCmdSetLineStipple)vkGetDeviceProcAddr(real_device, "vkCmdSetLineStipple");
    if (!fp) {
        fp = (PFN_vkCmdSetLineStipple)vkGetDeviceProcAddr(real_device, "vkCmdSetLineStippleKHR");
    }
    if (!fp) {
        fp = (PFN_vkCmdSetLineStipple)vkGetDeviceProcAddr(real_device, "vkCmdSetLineStippleEXT");
    }
    if (!fp) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: vkCmdSetLineStipple not supported on device");
        return;
    }
    fp(real_cb, args->lineStippleFactor, args->lineStipplePattern);
}

static void server_dispatch_vkCmdSetDepthBias(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdSetDepthBias* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthBias");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthBias")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthBias");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthBias(real_cb,
                      args->depthBiasConstantFactor,
                      args->depthBiasClamp,
                      args->depthBiasSlopeFactor);
}

static void server_dispatch_vkCmdSetDepthBounds(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdSetDepthBounds* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthBounds");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthBounds")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthBounds");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthBounds(real_cb, args->minDepthBounds, args->maxDepthBounds);
}

static void server_dispatch_vkCmdSetStencilCompareMask(struct vn_dispatch_context* ctx,
                                                       struct vn_command_vkCmdSetStencilCompareMask* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetStencilCompareMask");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetStencilCompareMask")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetStencilCompareMask");
    if (!real_cb) {
        return;
    }
    vkCmdSetStencilCompareMask(real_cb, args->faceMask, args->compareMask);
}

static void server_dispatch_vkCmdSetStencilWriteMask(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCmdSetStencilWriteMask* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetStencilWriteMask");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetStencilWriteMask")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetStencilWriteMask");
    if (!real_cb) {
        return;
    }
    vkCmdSetStencilWriteMask(real_cb, args->faceMask, args->writeMask);
}

static void server_dispatch_vkCmdSetStencilReference(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCmdSetStencilReference* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetStencilReference");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetStencilReference")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetStencilReference");
    if (!real_cb) {
        return;
    }
    vkCmdSetStencilReference(real_cb, args->faceMask, args->reference);
}

static void server_dispatch_vkCmdSetViewportWithCount(struct vn_dispatch_context* ctx,
                                                      struct vn_command_vkCmdSetViewportWithCount* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetViewportWithCount (count=%u)",
           args->viewportCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetViewportWithCount")) {
        return;
    }
    if (!args->pViewports || args->viewportCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid viewport data for vkCmdSetViewportWithCount");
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetViewportWithCount");
    if (!real_cb) {
        return;
    }
    vkCmdSetViewportWithCount(real_cb, args->viewportCount, args->pViewports);
}

static void server_dispatch_vkCmdSetScissorWithCount(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCmdSetScissorWithCount* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetScissorWithCount (count=%u)",
           args->scissorCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetScissorWithCount")) {
        return;
    }
    if (!args->pScissors || args->scissorCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid scissor data for vkCmdSetScissorWithCount");
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetScissorWithCount");
    if (!real_cb) {
        return;
    }
    vkCmdSetScissorWithCount(real_cb, args->scissorCount, args->pScissors);
}

static void server_dispatch_vkCmdSetDepthTestEnable(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdSetDepthTestEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthTestEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthTestEnable")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthTestEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthTestEnable(real_cb, args->depthTestEnable);
}

static void server_dispatch_vkCmdSetDepthWriteEnable(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCmdSetDepthWriteEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthWriteEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthWriteEnable")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthWriteEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthWriteEnable(real_cb, args->depthWriteEnable);
}

static void server_dispatch_vkCmdSetDepthCompareOp(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdSetDepthCompareOp* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthCompareOp");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthCompareOp")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthCompareOp");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthCompareOp(real_cb, args->depthCompareOp);
}

static void server_dispatch_vkCmdSetDepthBoundsTestEnable(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdSetDepthBoundsTestEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthBoundsTestEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthBoundsTestEnable")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthBoundsTestEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthBoundsTestEnable(real_cb, args->depthBoundsTestEnable);
}

static void server_dispatch_vkCmdSetStencilTestEnable(struct vn_dispatch_context* ctx,
                                                      struct vn_command_vkCmdSetStencilTestEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetStencilTestEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetStencilTestEnable")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetStencilTestEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetStencilTestEnable(real_cb, args->stencilTestEnable);
}

static void server_dispatch_vkCmdSetStencilOp(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdSetStencilOp* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetStencilOp");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetStencilOp")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetStencilOp");
    if (!real_cb) {
        return;
    }
    vkCmdSetStencilOp(real_cb,
                      args->faceMask,
                      args->failOp,
                      args->passOp,
                      args->depthFailOp,
                      args->compareOp);
}

static void server_dispatch_vkCmdSetRasterizerDiscardEnable(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdSetRasterizerDiscardEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetRasterizerDiscardEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetRasterizerDiscardEnable")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetRasterizerDiscardEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetRasterizerDiscardEnable(real_cb, args->rasterizerDiscardEnable);
}

static void server_dispatch_vkCmdSetDepthBiasEnable(struct vn_dispatch_context* ctx,
                                                    struct vn_command_vkCmdSetDepthBiasEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDepthBiasEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDepthBiasEnable")) {
        return;
    }
    VkCommandBuffer real_cb = get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDepthBiasEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetDepthBiasEnable(real_cb, args->depthBiasEnable);
}

static void server_dispatch_vkCmdSetPrimitiveRestartEnable(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCmdSetPrimitiveRestartEnable* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetPrimitiveRestartEnable");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetPrimitiveRestartEnable")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetPrimitiveRestartEnable");
    if (!real_cb) {
        return;
    }
    vkCmdSetPrimitiveRestartEnable(real_cb, args->primitiveRestartEnable);
}

static void server_dispatch_vkCmdSetDeviceMask(struct vn_dispatch_context* ctx,
                                               struct vn_command_vkCmdSetDeviceMask* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetDeviceMask (mask=%u)", args->deviceMask);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetDeviceMask")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetDeviceMask");
    if (!real_cb) {
        return;
    }
    vkCmdSetDeviceMask(real_cb, args->deviceMask);
}

static void server_dispatch_vkCmdExecuteCommands(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdExecuteCommands* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdExecuteCommands (count=%u)",
           args->commandBufferCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdExecuteCommands")) {
        return;
    }
    if (!args->pCommandBuffers || args->commandBufferCount == 0) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid command buffer list for vkCmdExecuteCommands");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer real_primary =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdExecuteCommands");
    if (real_primary == VK_NULL_HANDLE) {
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }

    VkCommandBuffer* real_secondary =
        args->commandBufferCount > 0 ? calloc(args->commandBufferCount, sizeof(*real_secondary)) : NULL;
    if (!real_secondary) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for secondary list");
        server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
        return;
    }
    for (uint32_t i = 0; i < args->commandBufferCount; ++i) {
        VkCommandBuffer real_cb =
            get_real_command_buffer(state, args->pCommandBuffers[i], "vkCmdExecuteCommands");
        if (real_cb == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Secondary command buffer %u not tracked", i);
            server_state_bridge_mark_command_buffer_invalid(state, args->commandBuffer);
            free(real_secondary);
            return;
        }
        real_secondary[i] = real_cb;
    }

    vkCmdExecuteCommands(real_primary, args->commandBufferCount, real_secondary);
    free(real_secondary);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdExecuteCommands recorded");
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

static void server_dispatch_vkCmdDrawIndexed(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdDrawIndexed* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDrawIndexed (indices=%u inst=%u)",
           args->indexCount,
           args->instanceCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDrawIndexed")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDrawIndexed");
    if (!real_cb) {
        return;
    }
    vkCmdDrawIndexed(real_cb,
                     args->indexCount,
                     args->instanceCount,
                     args->firstIndex,
                     args->vertexOffset,
                     args->firstInstance);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDrawIndexed recorded");
}

static void server_dispatch_vkCmdDrawIndirect(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkCmdDrawIndirect* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDrawIndirect (drawCount=%u)", args->drawCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDrawIndirect")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDrawIndirect");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdDrawIndirect");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDrawIndirect(real_cb, real_buffer, args->offset, args->drawCount, args->stride);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDrawIndirect recorded");
}

static void server_dispatch_vkCmdDrawIndirectCount(struct vn_dispatch_context* ctx,
                                                   struct vn_command_vkCmdDrawIndirectCount* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDrawIndirectCount (maxDrawCount=%u)",
           args->maxDrawCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDrawIndirectCount")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDrawIndirectCount");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdDrawIndirectCount");
    VkBuffer real_count = get_real_buffer(state, args->countBuffer, "vkCmdDrawIndirectCount");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE || real_count == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDrawIndirectCount(real_cb,
                           real_buffer,
                           args->offset,
                           real_count,
                           args->countBufferOffset,
                           args->maxDrawCount,
                           args->stride);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDrawIndirectCount recorded");
}

static void server_dispatch_vkCmdDrawIndexedIndirect(struct vn_dispatch_context* ctx,
                                                     struct vn_command_vkCmdDrawIndexedIndirect* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDrawIndexedIndirect (drawCount=%u)", args->drawCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDrawIndexedIndirect")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDrawIndexedIndirect");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdDrawIndexedIndirect");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDrawIndexedIndirect(real_cb, real_buffer, args->offset, args->drawCount, args->stride);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDrawIndexedIndirect recorded");
}

static void server_dispatch_vkCmdDrawIndexedIndirectCount(struct vn_dispatch_context* ctx,
                                                          struct vn_command_vkCmdDrawIndexedIndirectCount* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdDrawIndexedIndirectCount (maxDrawCount=%u)",
           args->maxDrawCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdDrawIndexedIndirectCount")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdDrawIndexedIndirectCount");
    VkBuffer real_buffer = get_real_buffer(state, args->buffer, "vkCmdDrawIndexedIndirectCount");
    VkBuffer real_count = get_real_buffer(state, args->countBuffer, "vkCmdDrawIndexedIndirectCount");
    if (real_cb == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE || real_count == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDrawIndexedIndirectCount(real_cb,
                                  real_buffer,
                                  args->offset,
                                  real_count,
                                  args->countBufferOffset,
                                  args->maxDrawCount,
                                  args->stride);
    VP_LOG_INFO(SERVER, "[Venus Server]   -> vkCmdDrawIndexedIndirectCount recorded");
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

static void server_dispatch_vkCmdPipelineBarrier2(struct vn_dispatch_context* ctx,
                                                  struct vn_command_vkCmdPipelineBarrier2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdPipelineBarrier2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdPipelineBarrier2")) {
        return;
    }
    if (!args->pDependencyInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing dependency info");
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdPipelineBarrier2");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }

    VkDependencyInfo info;
    VkMemoryBarrier2* memory = NULL;
    VkBufferMemoryBarrier2* buffers = NULL;
    VkImageMemoryBarrier2* images = NULL;
    if (!convert_dependency_info(state,
                                 args->pDependencyInfo,
                                 &info,
                                 &memory,
                                 &buffers,
                                 &images,
                                 "vkCmdPipelineBarrier2")) {
        free(memory);
        free(buffers);
        free(images);
        return;
    }

    vkCmdPipelineBarrier2(real_cb, &info);
    free(memory);
    free(buffers);
    free(images);
}

static void server_dispatch_vkCmdResetQueryPool(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdResetQueryPool* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdResetQueryPool");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdResetQueryPool")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdResetQueryPool");
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return;
    }
    vkCmdResetQueryPool(real_cb, real_pool, args->firstQuery, args->queryCount);
}

static void server_dispatch_vkCmdBeginQuery(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdBeginQuery* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdBeginQuery");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdBeginQuery")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdBeginQuery");
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBeginQuery(real_cb, real_pool, args->query, args->flags);
}

static void server_dispatch_vkCmdEndQuery(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCmdEndQuery* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdEndQuery");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdEndQuery")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdEndQuery");
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndQuery(real_cb, real_pool, args->query);
}

static void server_dispatch_vkCmdWriteTimestamp(struct vn_dispatch_context* ctx,
                                                struct vn_command_vkCmdWriteTimestamp* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdWriteTimestamp");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdWriteTimestamp")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdWriteTimestamp");
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return;
    }
    vkCmdWriteTimestamp(real_cb, args->pipelineStage, real_pool, args->query);
}

static void server_dispatch_vkCmdWriteTimestamp2(struct vn_dispatch_context* ctx,
                                                 struct vn_command_vkCmdWriteTimestamp2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdWriteTimestamp2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdWriteTimestamp2")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdWriteTimestamp2");
    VkQueryPool real_pool =
        server_state_bridge_get_real_query_pool(state, args->queryPool);
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return;
    }
    vkCmdWriteTimestamp2(real_cb, args->stage, real_pool, args->query);
}

static void server_dispatch_vkCmdCopyQueryPoolResults(struct vn_dispatch_context* ctx,
                                                      struct vn_command_vkCmdCopyQueryPoolResults* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdCopyQueryPoolResults");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdCopyQueryPoolResults")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdCopyQueryPoolResults");
    VkQueryPool real_pool = server_state_bridge_get_real_query_pool(state, args->queryPool);
    VkBuffer real_buffer = get_real_buffer(state, args->dstBuffer, "vkCmdCopyQueryPoolResults");
    if (real_cb == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdCopyQueryPoolResults(real_cb,
                              real_pool,
                              args->firstQuery,
                              args->queryCount,
                              real_buffer,
                              args->dstOffset,
                              args->stride,
                              args->flags);
}

static void server_dispatch_vkCmdSetEvent(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCmdSetEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetEvent")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetEvent");
    VkEvent real_event = server_state_bridge_get_real_event(state, args->event);
    if (real_cb == VK_NULL_HANDLE || real_event == VK_NULL_HANDLE) {
        return;
    }
    vkCmdSetEvent(real_cb, real_event, args->stageMask);
}

static void server_dispatch_vkCmdSetEvent2(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkCmdSetEvent2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdSetEvent2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdSetEvent2")) {
        return;
    }
    if (!args->pDependencyInfo) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Missing dependency info");
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdSetEvent2");
    VkEvent real_event = server_state_bridge_get_real_event(state, args->event);
    if (real_cb == VK_NULL_HANDLE || real_event == VK_NULL_HANDLE) {
        return;
    }
    VkDependencyInfo info;
    VkMemoryBarrier2* memory = NULL;
    VkBufferMemoryBarrier2* buffers = NULL;
    VkImageMemoryBarrier2* images = NULL;
    if (!convert_dependency_info(state,
                                 args->pDependencyInfo,
                                 &info,
                                 &memory,
                                 &buffers,
                                 &images,
                                 "vkCmdSetEvent2")) {
        free(memory);
        free(buffers);
        free(images);
        return;
    }
    vkCmdSetEvent2(real_cb, real_event, &info);
    free(memory);
    free(buffers);
    free(images);
}

static void server_dispatch_vkCmdResetEvent(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdResetEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdResetEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdResetEvent")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdResetEvent");
    VkEvent real_event = server_state_bridge_get_real_event(state, args->event);
    if (real_cb == VK_NULL_HANDLE || real_event == VK_NULL_HANDLE) {
        return;
    }
    vkCmdResetEvent(real_cb, real_event, args->stageMask);
}

static void server_dispatch_vkCmdResetEvent2(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdResetEvent2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdResetEvent2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdResetEvent2")) {
        return;
    }
    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdResetEvent2");
    VkEvent real_event = server_state_bridge_get_real_event(state, args->event);
    if (real_cb == VK_NULL_HANDLE || real_event == VK_NULL_HANDLE) {
        return;
    }
    vkCmdResetEvent2(real_cb, real_event, args->stageMask);
}

static void server_dispatch_vkCmdWaitEvents(struct vn_dispatch_context* ctx,
                                            struct vn_command_vkCmdWaitEvents* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdWaitEvents");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdWaitEvents")) {
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdWaitEvents");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }

    uint32_t eventCount = args->eventCount;
    VkEvent* real_events = NULL;
    if (eventCount > 0) {
        real_events = calloc(eventCount, sizeof(VkEvent));
        if (!real_events) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for events");
            return;
        }
        for (uint32_t i = 0; i < eventCount; ++i) {
            real_events[i] = server_state_bridge_get_real_event(state, args->pEvents[i]);
            if (real_events[i] == VK_NULL_HANDLE) {
                VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Unknown event in vkCmdWaitEvents");
                free(real_events);
                return;
            }
        }
    }

    VkBufferMemoryBarrier* buffer_barriers = NULL;
    if (args->bufferMemoryBarrierCount > 0) {
        buffer_barriers =
            calloc(args->bufferMemoryBarrierCount, sizeof(VkBufferMemoryBarrier));
        if (!buffer_barriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for buffer barriers");
            free(real_events);
            return;
        }
        for (uint32_t i = 0; i < args->bufferMemoryBarrierCount; ++i) {
            buffer_barriers[i] = args->pBufferMemoryBarriers[i];
            buffer_barriers[i].buffer =
                get_real_buffer(state, args->pBufferMemoryBarriers[i].buffer, "vkCmdWaitEvents");
            if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
                free(buffer_barriers);
                free(real_events);
                return;
            }
        }
    }

    VkImageMemoryBarrier* image_barriers = NULL;
    if (args->imageMemoryBarrierCount > 0) {
        image_barriers =
            calloc(args->imageMemoryBarrierCount, sizeof(VkImageMemoryBarrier));
        if (!image_barriers) {
            VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory for image barriers");
            free(buffer_barriers);
            free(real_events);
            return;
        }
        for (uint32_t i = 0; i < args->imageMemoryBarrierCount; ++i) {
            image_barriers[i] = args->pImageMemoryBarriers[i];
            image_barriers[i].image =
                get_real_image(state, args->pImageMemoryBarriers[i].image, "vkCmdWaitEvents");
            if (image_barriers[i].image == VK_NULL_HANDLE) {
                free(image_barriers);
                free(buffer_barriers);
                free(real_events);
                return;
            }
        }
    }

    vkCmdWaitEvents(real_cb,
                    eventCount,
                    real_events,
                    args->srcStageMask,
                    args->dstStageMask,
                    args->memoryBarrierCount,
                    args->pMemoryBarriers,
                    args->bufferMemoryBarrierCount,
                    buffer_barriers,
                    args->imageMemoryBarrierCount,
                    image_barriers);

    free(real_events);
    free(buffer_barriers);
    free(image_barriers);
}

static void server_dispatch_vkCmdWaitEvents2(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkCmdWaitEvents2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCmdWaitEvents2");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (!command_buffer_recording_guard(state, args->commandBuffer, "vkCmdWaitEvents2")) {
        return;
    }
    if (args->eventCount == 0 || !args->pEvents || !args->pDependencyInfos) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Invalid parameters for vkCmdWaitEvents2");
        return;
    }

    VkCommandBuffer real_cb =
        get_real_command_buffer(state, args->commandBuffer, "vkCmdWaitEvents2");
    if (real_cb == VK_NULL_HANDLE) {
        return;
    }

    VkEvent* real_events = calloc(args->eventCount, sizeof(*real_events));
    VkDependencyInfo* infos = calloc(args->eventCount, sizeof(*infos));
    VkMemoryBarrier2** memory_arrays = calloc(args->eventCount, sizeof(*memory_arrays));
    VkBufferMemoryBarrier2** buffer_arrays = calloc(args->eventCount, sizeof(*buffer_arrays));
    VkImageMemoryBarrier2** image_arrays = calloc(args->eventCount, sizeof(*image_arrays));
    if (!real_events || !infos || !memory_arrays || !buffer_arrays || !image_arrays) {
        VP_LOG_ERROR(SERVER, "[Venus Server]   -> ERROR: Out of memory in vkCmdWaitEvents2");
        free(real_events);
        free(infos);
        free(memory_arrays);
        free(buffer_arrays);
        free(image_arrays);
        return;
    }

    bool success = true;
    for (uint32_t i = 0; i < args->eventCount; ++i) {
        real_events[i] = server_state_bridge_get_real_event(state, args->pEvents[i]);
        if (real_events[i] == VK_NULL_HANDLE) {
            VP_LOG_ERROR(SERVER,
                         "[Venus Server]   -> ERROR: Unknown event in vkCmdWaitEvents2 (index=%u)",
                         i);
            success = false;
            break;
        }
        if (!convert_dependency_info(state,
                                     &args->pDependencyInfos[i],
                                     &infos[i],
                                     &memory_arrays[i],
                                     &buffer_arrays[i],
                                     &image_arrays[i],
                                     "vkCmdWaitEvents2")) {
            success = false;
            break;
        }
    }

    if (success) {
        vkCmdWaitEvents2(real_cb, args->eventCount, real_events, infos);
    }

    for (uint32_t i = 0; i < args->eventCount; ++i) {
        free(memory_arrays[i]);
        free(buffer_arrays[i]);
        free(image_arrays[i]);
    }
    free(real_events);
    free(infos);
    free(memory_arrays);
    free(buffer_arrays);
    free(image_arrays);
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

static void server_dispatch_vkCreateEvent(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkCreateEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkCreateEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_ERROR_INITIALIZATION_FAILED;
    if (!args->pEvent || !args->pCreateInfo) {
        return;
    }
    VkEvent event = server_state_bridge_create_event(state, args->device, args->pCreateInfo);
    if (event == VK_NULL_HANDLE) {
        return;
    }
    *args->pEvent = event;
    args->ret = VK_SUCCESS;
}

static void server_dispatch_vkDestroyEvent(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkDestroyEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkDestroyEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    if (args->event != VK_NULL_HANDLE) {
        server_state_bridge_destroy_event(state, args->event);
    }
}

static void server_dispatch_vkGetEventStatus(struct vn_dispatch_context* ctx,
                                             struct vn_command_vkGetEventStatus* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkGetEventStatus");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_get_event_status(state, args->event);
}

static void server_dispatch_vkSetEvent(struct vn_dispatch_context* ctx,
                                       struct vn_command_vkSetEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkSetEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_set_event(state, args->event);
}

static void server_dispatch_vkResetEvent(struct vn_dispatch_context* ctx,
                                         struct vn_command_vkResetEvent* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkResetEvent");
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_reset_event(state, args->event);
}

static void server_dispatch_vkQueueBindSparse(struct vn_dispatch_context* ctx,
                                              struct vn_command_vkQueueBindSparse* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkQueueBindSparse (bindInfoCount=%u)",
           args->bindInfoCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = VK_SUCCESS;

    if (args->bindInfoCount > 0 && !args->pBindInfo) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkQueue real_queue = server_state_bridge_get_real_queue(state, args->queue);
    if (args->queue != VK_NULL_HANDLE && real_queue == VK_NULL_HANDLE) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkFence real_fence = server_state_bridge_get_real_fence(state, args->fence);

    VkBindSparseInfo* infos =
        args->bindInfoCount > 0 ? calloc(args->bindInfoCount, sizeof(*infos)) : NULL;
    if (args->bindInfoCount > 0 && !infos) {
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }

    struct SparseTemp {
        VkSemaphore* wait_semaphores;
        VkSemaphore* signal_semaphores;
        VkSparseBufferMemoryBindInfo* buffer_infos;
        VkSparseMemoryBind** buffer_binds;
        VkSparseImageOpaqueMemoryBindInfo* image_opaque_infos;
        VkSparseMemoryBind** opaque_binds;
        VkSparseImageMemoryBindInfo* image_infos;
        VkSparseImageMemoryBind** image_binds;
        VkTimelineSemaphoreSubmitInfo timeline_info;
        uint64_t* wait_values;
        uint64_t* signal_values;
        bool has_timeline;
    };

    struct SparseTemp* temps =
        args->bindInfoCount > 0 ? calloc(args->bindInfoCount, sizeof(*temps)) : NULL;
    if (args->bindInfoCount > 0 && !temps) {
        free(infos);
        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }

    for (uint32_t i = 0; i < args->bindInfoCount; ++i) {
        const VkBindSparseInfo* src = &args->pBindInfo[i];
        VkBindSparseInfo* dst = &infos[i];
        struct SparseTemp* temp = &temps[i];
        *dst = *src;

        if (src->waitSemaphoreCount > 0) {
            temp->wait_semaphores = calloc(src->waitSemaphoreCount, sizeof(VkSemaphore));
            if (!temp->wait_semaphores) {
                args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->waitSemaphoreCount; ++j) {
                if (!server_state_bridge_semaphore_exists(state, src->pWaitSemaphores[j])) {
                    args->ret = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
                temp->wait_semaphores[j] =
                    server_state_bridge_get_real_semaphore(state, src->pWaitSemaphores[j]);
            }
            dst->pWaitSemaphores = temp->wait_semaphores;
        } else {
            dst->pWaitSemaphores = NULL;
        }

        if (src->bufferBindCount > 0) {
            temp->buffer_infos = calloc(src->bufferBindCount, sizeof(VkSparseBufferMemoryBindInfo));
            temp->buffer_binds = calloc(src->bufferBindCount, sizeof(VkSparseMemoryBind*));
            if (!temp->buffer_infos || !temp->buffer_binds) {
                args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->bufferBindCount; ++j) {
                const VkSparseBufferMemoryBindInfo* buf = &src->pBufferBinds[j];
                VkSparseBufferMemoryBindInfo* dst_buf = &temp->buffer_infos[j];
                *dst_buf = *buf;
                dst_buf->buffer = get_real_buffer(state, buf->buffer, "vkQueueBindSparse");
                if (dst_buf->buffer == VK_NULL_HANDLE) {
                    args->ret = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
                if (buf->bindCount > 0) {
                    temp->buffer_binds[j] =
                        calloc(buf->bindCount, sizeof(VkSparseMemoryBind));
                    if (!temp->buffer_binds[j]) {
                        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto cleanup;
                    }
                    memcpy(temp->buffer_binds[j], buf->pBinds, sizeof(VkSparseMemoryBind) * buf->bindCount);
                    for (uint32_t k = 0; k < buf->bindCount; ++k) {
                        if (temp->buffer_binds[j][k].memory != VK_NULL_HANDLE) {
                            temp->buffer_binds[j][k].memory = server_state_bridge_get_real_memory(
                                state, temp->buffer_binds[j][k].memory);
                            if (temp->buffer_binds[j][k].memory == VK_NULL_HANDLE) {
                                args->ret = VK_ERROR_INITIALIZATION_FAILED;
                                goto cleanup;
                            }
                        }
                    }
                    dst_buf->pBinds = temp->buffer_binds[j];
                } else {
                    dst_buf->pBinds = NULL;
                }
            }
            dst->pBufferBinds = temp->buffer_infos;
        } else {
            dst->pBufferBinds = NULL;
        }

        if (src->imageOpaqueBindCount > 0) {
            temp->image_opaque_infos =
                calloc(src->imageOpaqueBindCount, sizeof(VkSparseImageOpaqueMemoryBindInfo));
            temp->opaque_binds = calloc(src->imageOpaqueBindCount, sizeof(VkSparseMemoryBind*));
            if (!temp->image_opaque_infos || !temp->opaque_binds) {
                args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->imageOpaqueBindCount; ++j) {
                const VkSparseImageOpaqueMemoryBindInfo* info = &src->pImageOpaqueBinds[j];
                VkSparseImageOpaqueMemoryBindInfo* dst_info = &temp->image_opaque_infos[j];
                *dst_info = *info;
                dst_info->image = get_real_image(state, info->image, "vkQueueBindSparse");
                if (dst_info->image == VK_NULL_HANDLE) {
                    args->ret = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
                if (info->bindCount > 0) {
                    temp->opaque_binds[j] =
                        calloc(info->bindCount, sizeof(VkSparseMemoryBind));
                    if (!temp->opaque_binds[j]) {
                        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto cleanup;
                    }
                    memcpy(temp->opaque_binds[j], info->pBinds, sizeof(VkSparseMemoryBind) * info->bindCount);
                    for (uint32_t k = 0; k < info->bindCount; ++k) {
                        if (temp->opaque_binds[j][k].memory != VK_NULL_HANDLE) {
                            temp->opaque_binds[j][k].memory = server_state_bridge_get_real_memory(
                                state, temp->opaque_binds[j][k].memory);
                            if (temp->opaque_binds[j][k].memory == VK_NULL_HANDLE) {
                                args->ret = VK_ERROR_INITIALIZATION_FAILED;
                                goto cleanup;
                            }
                        }
                    }
                    dst_info->pBinds = temp->opaque_binds[j];
                } else {
                    dst_info->pBinds = NULL;
                }
            }
            dst->pImageOpaqueBinds = temp->image_opaque_infos;
        } else {
            dst->pImageOpaqueBinds = NULL;
        }

        if (src->imageBindCount > 0) {
            temp->image_infos =
                calloc(src->imageBindCount, sizeof(VkSparseImageMemoryBindInfo));
            temp->image_binds = calloc(src->imageBindCount, sizeof(VkSparseImageMemoryBind*));
            if (!temp->image_infos || !temp->image_binds) {
                args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->imageBindCount; ++j) {
                const VkSparseImageMemoryBindInfo* info = &src->pImageBinds[j];
                VkSparseImageMemoryBindInfo* dst_info = &temp->image_infos[j];
                *dst_info = *info;
                dst_info->image = get_real_image(state, info->image, "vkQueueBindSparse");
                if (dst_info->image == VK_NULL_HANDLE) {
                    args->ret = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
                if (info->bindCount > 0) {
                    temp->image_binds[j] =
                        calloc(info->bindCount, sizeof(VkSparseImageMemoryBind));
                    if (!temp->image_binds[j]) {
                        args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                        goto cleanup;
                    }
                    memcpy(temp->image_binds[j], info->pBinds, sizeof(VkSparseImageMemoryBind) * info->bindCount);
                    for (uint32_t k = 0; k < info->bindCount; ++k) {
                        if (temp->image_binds[j][k].memory != VK_NULL_HANDLE) {
                            temp->image_binds[j][k].memory = server_state_bridge_get_real_memory(
                                state, temp->image_binds[j][k].memory);
                            if (temp->image_binds[j][k].memory == VK_NULL_HANDLE) {
                                args->ret = VK_ERROR_INITIALIZATION_FAILED;
                                goto cleanup;
                            }
                        }
                    }
                    dst_info->pBinds = temp->image_binds[j];
                } else {
                    dst_info->pBinds = NULL;
                }
            }
            dst->pImageBinds = temp->image_infos;
        } else {
            dst->pImageBinds = NULL;
        }

        if (src->signalSemaphoreCount > 0) {
            temp->signal_semaphores = calloc(src->signalSemaphoreCount, sizeof(VkSemaphore));
            if (!temp->signal_semaphores) {
                args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto cleanup;
            }
            for (uint32_t j = 0; j < src->signalSemaphoreCount; ++j) {
                if (!server_state_bridge_semaphore_exists(state, src->pSignalSemaphores[j])) {
                    args->ret = VK_ERROR_INITIALIZATION_FAILED;
                    goto cleanup;
                }
                temp->signal_semaphores[j] =
                    server_state_bridge_get_real_semaphore(state, src->pSignalSemaphores[j]);
            }
            dst->pSignalSemaphores = temp->signal_semaphores;
        } else {
            dst->pSignalSemaphores = NULL;
        }

        const VkTimelineSemaphoreSubmitInfo* timeline = find_timeline_submit_info(src->pNext);
        temp->has_timeline = false;
        if (timeline) {
            temp->timeline_info = *timeline;
            if (timeline->waitSemaphoreValueCount > 0) {
                temp->wait_values = calloc(timeline->waitSemaphoreValueCount, sizeof(uint64_t));
                if (!temp->wait_values) {
                    args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto cleanup;
                }
                memcpy(temp->wait_values,
                       timeline->pWaitSemaphoreValues,
                       timeline->waitSemaphoreValueCount * sizeof(uint64_t));
                temp->timeline_info.pWaitSemaphoreValues = temp->wait_values;
            }
            if (timeline->signalSemaphoreValueCount > 0) {
                temp->signal_values = calloc(timeline->signalSemaphoreValueCount, sizeof(uint64_t));
                if (!temp->signal_values) {
                    args->ret = VK_ERROR_OUT_OF_HOST_MEMORY;
                    goto cleanup;
                }
                memcpy(temp->signal_values,
                       timeline->pSignalSemaphoreValues,
                       timeline->signalSemaphoreValueCount * sizeof(uint64_t));
                temp->timeline_info.pSignalSemaphoreValues = temp->signal_values;
            }
            dst->pNext = &temp->timeline_info;
            temp->has_timeline = true;
        } else {
            dst->pNext = NULL;
        }
    }

    args->ret = vkQueueBindSparse(real_queue, args->bindInfoCount, infos, real_fence);

cleanup:
    if (temps) {
        for (uint32_t i = 0; i < args->bindInfoCount; ++i) {
            free(temps[i].wait_semaphores);
            free(temps[i].signal_semaphores);
            if (temps[i].buffer_binds) {
                for (uint32_t j = 0; j < args->pBindInfo[i].bufferBindCount; ++j) {
                    free(temps[i].buffer_binds[j]);
                }
            }
            if (temps[i].opaque_binds) {
                for (uint32_t j = 0; j < args->pBindInfo[i].imageOpaqueBindCount; ++j) {
                    free(temps[i].opaque_binds[j]);
                }
            }
            if (temps[i].image_binds) {
                for (uint32_t j = 0; j < args->pBindInfo[i].imageBindCount; ++j) {
                    free(temps[i].image_binds[j]);
                }
            }
            free(temps[i].buffer_infos);
            free(temps[i].buffer_binds);
            free(temps[i].image_opaque_infos);
            free(temps[i].opaque_binds);
            free(temps[i].image_infos);
            free(temps[i].image_binds);
            free(temps[i].wait_values);
            free(temps[i].signal_values);
        }
    }
    free(temps);
    free(infos);
}
static void server_dispatch_vkQueueSubmit(struct vn_dispatch_context* ctx,
                                          struct vn_command_vkQueueSubmit* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkQueueSubmit (submitCount=%u)", args->submitCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_submit(state, args->queue, args->submitCount, args->pSubmits, args->fence);
}

static void server_dispatch_vkQueueSubmit2(struct vn_dispatch_context* ctx,
                                           struct vn_command_vkQueueSubmit2* args) {
    VP_LOG_INFO(SERVER, "[Venus Server] Dispatching vkQueueSubmit2 (submitCount=%u)", args->submitCount);
    struct ServerState* state = (struct ServerState*)ctx->data;
    args->ret = server_state_bridge_queue_submit2(state,
                                                  args->queue,
                                                  args->submitCount,
                                                  args->pSubmits,
                                                  args->fence);
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
    renderer->ctx.dispatch_vkEnumeratePhysicalDeviceGroups = server_dispatch_vkEnumeratePhysicalDeviceGroups;

    // Phase 3 handlers: Physical device queries
    renderer->ctx.dispatch_vkGetPhysicalDeviceProperties = server_dispatch_vkGetPhysicalDeviceProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFeatures = server_dispatch_vkGetPhysicalDeviceFeatures;
    renderer->ctx.dispatch_vkGetPhysicalDeviceQueueFamilyProperties = server_dispatch_vkGetPhysicalDeviceQueueFamilyProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceMemoryProperties = server_dispatch_vkGetPhysicalDeviceMemoryProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFormatProperties = server_dispatch_vkGetPhysicalDeviceFormatProperties;
    renderer->ctx.dispatch_vkGetPhysicalDeviceFormatProperties2 = server_dispatch_vkGetPhysicalDeviceFormatProperties2;
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
    renderer->ctx.dispatch_vkGetDeviceQueue2 = server_dispatch_vkGetDeviceQueue2;
    renderer->ctx.dispatch_vkGetDeviceGroupPeerMemoryFeatures =
        server_dispatch_vkGetDeviceGroupPeerMemoryFeatures;

    // Phase 4 handlers: Memory and resources
    renderer->ctx.dispatch_vkAllocateMemory = server_dispatch_vkAllocateMemory;
    renderer->ctx.dispatch_vkMapMemory = server_dispatch_vkMapMemory;
    renderer->ctx.dispatch_vkUnmapMemory = server_dispatch_vkUnmapMemory;
    renderer->ctx.dispatch_vkMapMemory2 = server_dispatch_vkMapMemory2;
    renderer->ctx.dispatch_vkUnmapMemory2 = server_dispatch_vkUnmapMemory2;
    renderer->ctx.dispatch_vkFreeMemory = server_dispatch_vkFreeMemory;
    renderer->ctx.dispatch_vkGetDeviceMemoryCommitment = server_dispatch_vkGetDeviceMemoryCommitment;
    renderer->ctx.dispatch_vkCreateBuffer = server_dispatch_vkCreateBuffer;
    renderer->ctx.dispatch_vkDestroyBuffer = server_dispatch_vkDestroyBuffer;
    renderer->ctx.dispatch_vkGetBufferMemoryRequirements = server_dispatch_vkGetBufferMemoryRequirements;
    renderer->ctx.dispatch_vkGetBufferMemoryRequirements2 = server_dispatch_vkGetBufferMemoryRequirements2;
    renderer->ctx.dispatch_vkBindBufferMemory = server_dispatch_vkBindBufferMemory;
    renderer->ctx.dispatch_vkBindBufferMemory2 = server_dispatch_vkBindBufferMemory2;
    renderer->ctx.dispatch_vkGetBufferDeviceAddress = server_dispatch_vkGetBufferDeviceAddress;
    renderer->ctx.dispatch_vkGetBufferOpaqueCaptureAddress = server_dispatch_vkGetBufferOpaqueCaptureAddress;
    renderer->ctx.dispatch_vkGetDeviceMemoryOpaqueCaptureAddress =
        server_dispatch_vkGetDeviceMemoryOpaqueCaptureAddress;
    renderer->ctx.dispatch_vkCreateImage = server_dispatch_vkCreateImage;
    renderer->ctx.dispatch_vkDestroyImage = server_dispatch_vkDestroyImage;
    renderer->ctx.dispatch_vkGetImageMemoryRequirements2 = server_dispatch_vkGetImageMemoryRequirements2;
    renderer->ctx.dispatch_vkGetImageMemoryRequirements = server_dispatch_vkGetImageMemoryRequirements;
    renderer->ctx.dispatch_vkGetDeviceBufferMemoryRequirements =
        server_dispatch_vkGetDeviceBufferMemoryRequirements;
    renderer->ctx.dispatch_vkGetDeviceImageMemoryRequirements =
        server_dispatch_vkGetDeviceImageMemoryRequirements;
    renderer->ctx.dispatch_vkGetDeviceImageSparseMemoryRequirements =
        server_dispatch_vkGetDeviceImageSparseMemoryRequirements;
    renderer->ctx.dispatch_vkBindImageMemory = server_dispatch_vkBindImageMemory;
    renderer->ctx.dispatch_vkBindImageMemory2 = server_dispatch_vkBindImageMemory2;
    renderer->ctx.dispatch_vkGetImageSubresourceLayout = server_dispatch_vkGetImageSubresourceLayout;
    renderer->ctx.dispatch_vkGetImageSubresourceLayout2 = server_dispatch_vkGetImageSubresourceLayout2;
    renderer->ctx.dispatch_vkGetDeviceImageSubresourceLayout =
        server_dispatch_vkGetDeviceImageSubresourceLayout;
    renderer->ctx.dispatch_vkCopyMemoryToImage = server_dispatch_vkCopyMemoryToImage;
    renderer->ctx.dispatch_vkCopyImageToMemory = server_dispatch_vkCopyImageToMemory;
    renderer->ctx.dispatch_vkCopyImageToMemoryMESA = server_dispatch_vkCopyImageToMemoryMESA;
    renderer->ctx.dispatch_vkCopyMemoryToImageMESA = server_dispatch_vkCopyMemoryToImageMESA;
    renderer->ctx.dispatch_vkCopyImageToImage = server_dispatch_vkCopyImageToImage;
    renderer->ctx.dispatch_vkTransitionImageLayout = server_dispatch_vkTransitionImageLayout;
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
    renderer->ctx.dispatch_vkCmdPushDescriptorSet = server_dispatch_vkCmdPushDescriptorSet;
    renderer->ctx.dispatch_vkCmdPushDescriptorSetWithTemplate =
        server_dispatch_vkCmdPushDescriptorSetWithTemplate;
    renderer->ctx.dispatch_vkCmdPushDescriptorSet2 = server_dispatch_vkCmdPushDescriptorSet2;
    renderer->ctx.dispatch_vkCmdPushDescriptorSetWithTemplate2 =
        server_dispatch_vkCmdPushDescriptorSetWithTemplate2;
    renderer->ctx.dispatch_vkCmdPushDescriptorSet2 = server_dispatch_vkCmdPushDescriptorSet2;
    renderer->ctx.dispatch_vkCmdPushDescriptorSetWithTemplate2 =
        server_dispatch_vkCmdPushDescriptorSetWithTemplate2;
    renderer->ctx.dispatch_vkCreateDescriptorUpdateTemplate = server_dispatch_vkCreateDescriptorUpdateTemplate;
    renderer->ctx.dispatch_vkDestroyDescriptorUpdateTemplate = server_dispatch_vkDestroyDescriptorUpdateTemplate;
    renderer->ctx.dispatch_vkCreatePipelineLayout = server_dispatch_vkCreatePipelineLayout;
    renderer->ctx.dispatch_vkDestroyPipelineLayout = server_dispatch_vkDestroyPipelineLayout;
    renderer->ctx.dispatch_vkCreatePipelineCache = server_dispatch_vkCreatePipelineCache;
    renderer->ctx.dispatch_vkDestroyPipelineCache = server_dispatch_vkDestroyPipelineCache;
    renderer->ctx.dispatch_vkGetPipelineCacheData = server_dispatch_vkGetPipelineCacheData;
    renderer->ctx.dispatch_vkMergePipelineCaches = server_dispatch_vkMergePipelineCaches;
    renderer->ctx.dispatch_vkCreateRenderPass = server_dispatch_vkCreateRenderPass;
    renderer->ctx.dispatch_vkCreateRenderPass2 = server_dispatch_vkCreateRenderPass2;
    renderer->ctx.dispatch_vkDestroyRenderPass = server_dispatch_vkDestroyRenderPass;
    renderer->ctx.dispatch_vkGetRenderAreaGranularity = server_dispatch_vkGetRenderAreaGranularity;
    renderer->ctx.dispatch_vkGetRenderingAreaGranularity = server_dispatch_vkGetRenderingAreaGranularity;
    renderer->ctx.dispatch_vkCreateFramebuffer = server_dispatch_vkCreateFramebuffer;
    renderer->ctx.dispatch_vkDestroyFramebuffer = server_dispatch_vkDestroyFramebuffer;
    renderer->ctx.dispatch_vkCreateComputePipelines = server_dispatch_vkCreateComputePipelines;
    renderer->ctx.dispatch_vkCreateGraphicsPipelines = server_dispatch_vkCreateGraphicsPipelines;
    renderer->ctx.dispatch_vkDestroyPipeline = server_dispatch_vkDestroyPipeline;
    renderer->ctx.dispatch_vkCreateCommandPool = server_dispatch_vkCreateCommandPool;
    renderer->ctx.dispatch_vkDestroyCommandPool = server_dispatch_vkDestroyCommandPool;
    renderer->ctx.dispatch_vkResetCommandPool = server_dispatch_vkResetCommandPool;
    renderer->ctx.dispatch_vkTrimCommandPool = server_dispatch_vkTrimCommandPool;
    renderer->ctx.dispatch_vkAllocateCommandBuffers = server_dispatch_vkAllocateCommandBuffers;
    renderer->ctx.dispatch_vkFreeCommandBuffers = server_dispatch_vkFreeCommandBuffers;
    renderer->ctx.dispatch_vkBeginCommandBuffer = server_dispatch_vkBeginCommandBuffer;
    renderer->ctx.dispatch_vkEndCommandBuffer = server_dispatch_vkEndCommandBuffer;
    renderer->ctx.dispatch_vkResetCommandBuffer = server_dispatch_vkResetCommandBuffer;
    renderer->ctx.dispatch_vkCmdCopyBuffer = server_dispatch_vkCmdCopyBuffer;
    renderer->ctx.dispatch_vkCmdCopyBuffer2 = server_dispatch_vkCmdCopyBuffer2;
    renderer->ctx.dispatch_vkCmdCopyImage = server_dispatch_vkCmdCopyImage;
    renderer->ctx.dispatch_vkCmdCopyImage2 = server_dispatch_vkCmdCopyImage2;
    renderer->ctx.dispatch_vkCmdBlitImage = server_dispatch_vkCmdBlitImage;
    renderer->ctx.dispatch_vkCmdBlitImage2 = server_dispatch_vkCmdBlitImage2;
    renderer->ctx.dispatch_vkCmdCopyBufferToImage = server_dispatch_vkCmdCopyBufferToImage;
    renderer->ctx.dispatch_vkCmdCopyBufferToImage2 = server_dispatch_vkCmdCopyBufferToImage2;
    renderer->ctx.dispatch_vkCmdCopyImageToBuffer = server_dispatch_vkCmdCopyImageToBuffer;
    renderer->ctx.dispatch_vkCmdCopyImageToBuffer2 = server_dispatch_vkCmdCopyImageToBuffer2;
    renderer->ctx.dispatch_vkCmdResolveImage = server_dispatch_vkCmdResolveImage;
    renderer->ctx.dispatch_vkCmdResolveImage2 = server_dispatch_vkCmdResolveImage2;
    renderer->ctx.dispatch_vkCmdFillBuffer = server_dispatch_vkCmdFillBuffer;
    renderer->ctx.dispatch_vkCmdUpdateBuffer = server_dispatch_vkCmdUpdateBuffer;
    renderer->ctx.dispatch_vkCmdClearColorImage = server_dispatch_vkCmdClearColorImage;
    renderer->ctx.dispatch_vkCmdClearDepthStencilImage = server_dispatch_vkCmdClearDepthStencilImage;
    renderer->ctx.dispatch_vkCmdClearAttachments = server_dispatch_vkCmdClearAttachments;
    renderer->ctx.dispatch_vkCmdBeginRenderPass = server_dispatch_vkCmdBeginRenderPass;
    renderer->ctx.dispatch_vkCmdBeginRenderPass2 = server_dispatch_vkCmdBeginRenderPass2;
    renderer->ctx.dispatch_vkCmdEndRenderPass = server_dispatch_vkCmdEndRenderPass;
    renderer->ctx.dispatch_vkCmdEndRenderPass2 = server_dispatch_vkCmdEndRenderPass2;
    renderer->ctx.dispatch_vkCmdBeginRendering = server_dispatch_vkCmdBeginRendering;
    renderer->ctx.dispatch_vkCmdEndRendering = server_dispatch_vkCmdEndRendering;
    renderer->ctx.dispatch_vkCmdSetRenderingAttachmentLocations =
        server_dispatch_vkCmdSetRenderingAttachmentLocations;
    renderer->ctx.dispatch_vkCmdSetRenderingInputAttachmentIndices =
        server_dispatch_vkCmdSetRenderingInputAttachmentIndices;
    renderer->ctx.dispatch_vkCmdBindPipeline = server_dispatch_vkCmdBindPipeline;
    renderer->ctx.dispatch_vkCmdBindIndexBuffer = server_dispatch_vkCmdBindIndexBuffer;
    renderer->ctx.dispatch_vkCmdBindIndexBuffer2 = server_dispatch_vkCmdBindIndexBuffer2;
    renderer->ctx.dispatch_vkCmdBindVertexBuffers = server_dispatch_vkCmdBindVertexBuffers;
    renderer->ctx.dispatch_vkCmdBindVertexBuffers2 = server_dispatch_vkCmdBindVertexBuffers2;
    renderer->ctx.dispatch_vkCmdBindDescriptorSets = server_dispatch_vkCmdBindDescriptorSets;
    renderer->ctx.dispatch_vkCmdBindDescriptorSets2 = server_dispatch_vkCmdBindDescriptorSets2;
    renderer->ctx.dispatch_vkCmdPushConstants = server_dispatch_vkCmdPushConstants;
    renderer->ctx.dispatch_vkCmdPushConstants2 = server_dispatch_vkCmdPushConstants2;
    renderer->ctx.dispatch_vkCmdDispatch = server_dispatch_vkCmdDispatch;
    renderer->ctx.dispatch_vkCmdDispatchIndirect = server_dispatch_vkCmdDispatchIndirect;
    renderer->ctx.dispatch_vkCmdDispatchBase = server_dispatch_vkCmdDispatchBase;
    renderer->ctx.dispatch_vkCmdSetBlendConstants = server_dispatch_vkCmdSetBlendConstants;
    renderer->ctx.dispatch_vkCmdSetLineWidth = server_dispatch_vkCmdSetLineWidth;
    renderer->ctx.dispatch_vkCmdSetLineStipple = server_dispatch_vkCmdSetLineStipple;
    renderer->ctx.dispatch_vkCmdSetDepthBias = server_dispatch_vkCmdSetDepthBias;
    renderer->ctx.dispatch_vkCmdSetDepthBounds = server_dispatch_vkCmdSetDepthBounds;
    renderer->ctx.dispatch_vkCmdSetStencilCompareMask = server_dispatch_vkCmdSetStencilCompareMask;
    renderer->ctx.dispatch_vkCmdSetStencilWriteMask = server_dispatch_vkCmdSetStencilWriteMask;
    renderer->ctx.dispatch_vkCmdSetStencilReference = server_dispatch_vkCmdSetStencilReference;
    renderer->ctx.dispatch_vkCmdSetDeviceMask = server_dispatch_vkCmdSetDeviceMask;
    renderer->ctx.dispatch_vkCmdSetViewport = server_dispatch_vkCmdSetViewport;
    renderer->ctx.dispatch_vkCmdSetViewportWithCount = server_dispatch_vkCmdSetViewportWithCount;
    renderer->ctx.dispatch_vkCmdSetScissor = server_dispatch_vkCmdSetScissor;
    renderer->ctx.dispatch_vkCmdSetScissorWithCount = server_dispatch_vkCmdSetScissorWithCount;
    renderer->ctx.dispatch_vkCmdSetCullMode = server_dispatch_vkCmdSetCullMode;
    renderer->ctx.dispatch_vkCmdSetFrontFace = server_dispatch_vkCmdSetFrontFace;
    renderer->ctx.dispatch_vkCmdSetPrimitiveTopology = server_dispatch_vkCmdSetPrimitiveTopology;
    renderer->ctx.dispatch_vkCmdSetDepthTestEnable = server_dispatch_vkCmdSetDepthTestEnable;
    renderer->ctx.dispatch_vkCmdSetDepthWriteEnable = server_dispatch_vkCmdSetDepthWriteEnable;
    renderer->ctx.dispatch_vkCmdSetDepthCompareOp = server_dispatch_vkCmdSetDepthCompareOp;
    renderer->ctx.dispatch_vkCmdSetDepthBoundsTestEnable = server_dispatch_vkCmdSetDepthBoundsTestEnable;
    renderer->ctx.dispatch_vkCmdSetStencilTestEnable = server_dispatch_vkCmdSetStencilTestEnable;
    renderer->ctx.dispatch_vkCmdSetStencilOp = server_dispatch_vkCmdSetStencilOp;
    renderer->ctx.dispatch_vkCmdSetRasterizerDiscardEnable = server_dispatch_vkCmdSetRasterizerDiscardEnable;
    renderer->ctx.dispatch_vkCmdSetDepthBiasEnable = server_dispatch_vkCmdSetDepthBiasEnable;
    renderer->ctx.dispatch_vkCmdSetPrimitiveRestartEnable = server_dispatch_vkCmdSetPrimitiveRestartEnable;
    renderer->ctx.dispatch_vkCmdNextSubpass = server_dispatch_vkCmdNextSubpass;
    renderer->ctx.dispatch_vkCmdNextSubpass2 = server_dispatch_vkCmdNextSubpass2;
    renderer->ctx.dispatch_vkCmdSetDeviceMask = server_dispatch_vkCmdSetDeviceMask;
    renderer->ctx.dispatch_vkCmdExecuteCommands = server_dispatch_vkCmdExecuteCommands;
    renderer->ctx.dispatch_vkCmdDraw = server_dispatch_vkCmdDraw;
    renderer->ctx.dispatch_vkCmdDrawIndexed = server_dispatch_vkCmdDrawIndexed;
    renderer->ctx.dispatch_vkCmdDrawIndirect = server_dispatch_vkCmdDrawIndirect;
    renderer->ctx.dispatch_vkCmdDrawIndirectCount = server_dispatch_vkCmdDrawIndirectCount;
    renderer->ctx.dispatch_vkCmdDrawIndexedIndirect = server_dispatch_vkCmdDrawIndexedIndirect;
    renderer->ctx.dispatch_vkCmdDrawIndexedIndirectCount = server_dispatch_vkCmdDrawIndexedIndirectCount;
    renderer->ctx.dispatch_vkCmdPipelineBarrier = server_dispatch_vkCmdPipelineBarrier;
    renderer->ctx.dispatch_vkCmdPipelineBarrier2 = server_dispatch_vkCmdPipelineBarrier2;
    renderer->ctx.dispatch_vkCmdResetQueryPool = server_dispatch_vkCmdResetQueryPool;
    renderer->ctx.dispatch_vkCmdBeginQuery = server_dispatch_vkCmdBeginQuery;
    renderer->ctx.dispatch_vkCmdEndQuery = server_dispatch_vkCmdEndQuery;
    renderer->ctx.dispatch_vkCmdWriteTimestamp = server_dispatch_vkCmdWriteTimestamp;
    renderer->ctx.dispatch_vkCmdWriteTimestamp2 = server_dispatch_vkCmdWriteTimestamp2;
    renderer->ctx.dispatch_vkCmdCopyQueryPoolResults = server_dispatch_vkCmdCopyQueryPoolResults;
    renderer->ctx.dispatch_vkCmdSetEvent = server_dispatch_vkCmdSetEvent;
    renderer->ctx.dispatch_vkCmdSetEvent2 = server_dispatch_vkCmdSetEvent2;
    renderer->ctx.dispatch_vkCmdResetEvent = server_dispatch_vkCmdResetEvent;
    renderer->ctx.dispatch_vkCmdResetEvent2 = server_dispatch_vkCmdResetEvent2;
    renderer->ctx.dispatch_vkCmdWaitEvents = server_dispatch_vkCmdWaitEvents;
    renderer->ctx.dispatch_vkCmdWaitEvents2 = server_dispatch_vkCmdWaitEvents2;
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
    renderer->ctx.dispatch_vkQueueBindSparse = server_dispatch_vkQueueBindSparse;
    renderer->ctx.dispatch_vkCreateEvent = server_dispatch_vkCreateEvent;
    renderer->ctx.dispatch_vkDestroyEvent = server_dispatch_vkDestroyEvent;
    renderer->ctx.dispatch_vkGetEventStatus = server_dispatch_vkGetEventStatus;
    renderer->ctx.dispatch_vkSetEvent = server_dispatch_vkSetEvent;
    renderer->ctx.dispatch_vkResetEvent = server_dispatch_vkResetEvent;
    renderer->ctx.dispatch_vkQueueSubmit = server_dispatch_vkQueueSubmit;
    renderer->ctx.dispatch_vkQueueSubmit2 = server_dispatch_vkQueueSubmit2;
    renderer->ctx.dispatch_vkQueueWaitIdle = server_dispatch_vkQueueWaitIdle;
    renderer->ctx.dispatch_vkDeviceWaitIdle = server_dispatch_vkDeviceWaitIdle;
    renderer->ctx.dispatch_vkCreateQueryPool = server_dispatch_vkCreateQueryPool;
    renderer->ctx.dispatch_vkDestroyQueryPool = server_dispatch_vkDestroyQueryPool;
    renderer->ctx.dispatch_vkResetQueryPool = server_dispatch_vkResetQueryPool;
    renderer->ctx.dispatch_vkGetQueryPoolResults = server_dispatch_vkGetQueryPoolResults;

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

    while (vn_cs_decoder_bytes_remaining(renderer->decoder) > 0 &&
           !vn_cs_decoder_get_fatal(renderer->decoder)) {
        vn_dispatch_command(&renderer->ctx);
    }

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
