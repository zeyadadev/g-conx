// Descriptor Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "profiling.h"

extern "C" {

// Vulkan function implementations

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorSetLayout* pSetLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDescriptorSetLayout called\n";

    if (!pCreateInfo || !pSetLayout) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateDescriptorSetLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateDescriptorSetLayout\n";
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
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDescriptorSetLayout failed: " << result << "\n";
        return result;
    }

    VkDescriptorSetLayout local = g_handle_allocator.allocate<VkDescriptorSetLayout>();
    g_pipeline_state.add_descriptor_set_layout(device, local, remote_layout);
    *pSetLayout = local;
    ICD_LOG_INFO() << "[Client ICD] Descriptor set layout created (local=" << local << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device,
    VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDescriptorSetLayout called\n";

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorSetLayout remote_layout =
        g_pipeline_state.get_remote_descriptor_set_layout(descriptorSetLayout);
    g_pipeline_state.remove_descriptor_set_layout(descriptorSetLayout);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor set layout handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorSetLayout(&g_ring,
                                          icd_device->remote_handle,
                                          remote_layout,
                                          pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Descriptor set layout destroyed (local=" << descriptorSetLayout
              << ")\n";
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device,
    const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorPool* pDescriptorPool) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDescriptorPool called\n";

    if (!pCreateInfo || !pDescriptorPool) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateDescriptorPool\n";
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
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDescriptorPool failed: " << result << "\n";
        return result;
    }

    VkDescriptorPool local = g_handle_allocator.allocate<VkDescriptorPool>();
    g_pipeline_state.add_descriptor_pool(device, local, remote_pool, pCreateInfo->flags);
    *pDescriptorPool = local;
    ICD_LOG_INFO() << "[Client ICD] Descriptor pool created (local=" << local << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    g_pipeline_state.remove_descriptor_pool(descriptorPool);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyDescriptorPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyDescriptorPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorPool(&g_ring,
                                     icd_device->remote_handle,
                                     remote_pool,
                                     pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Descriptor pool destroyed (local=" << descriptorPool << ")\n";
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    VkDescriptorPoolResetFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkResetDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
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
        ICD_LOG_ERROR() << "[Client ICD] vkResetDescriptorPool failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device,
    const VkDescriptorSetAllocateInfo* pAllocateInfo,
    VkDescriptorSet* pDescriptorSets) {

    ICD_LOG_INFO() << "[Client ICD] vkAllocateDescriptorSets called\n";

    if (!pAllocateInfo || (!pDescriptorSets && pAllocateInfo->descriptorSetCount > 0)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pAllocateInfo->descriptorSetCount == 0) {
        return VK_SUCCESS;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pAllocateInfo->pSetLayouts) {
        ICD_LOG_ERROR() << "[Client ICD] Layout array missing in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool =
        g_pipeline_state.get_remote_descriptor_pool(pAllocateInfo->descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSetLayout> remote_layouts(pAllocateInfo->descriptorSetCount);
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        remote_layouts[i] =
            g_pipeline_state.get_remote_descriptor_set_layout(pAllocateInfo->pSetLayouts[i]);
        if (remote_layouts[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set layout not tracked for allocation\n";
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
        ICD_LOG_ERROR() << "[Client ICD] vkAllocateDescriptorSets failed: " << result << "\n";
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

    ICD_LOG_INFO() << "[Client ICD] Allocated " << pAllocateInfo->descriptorSetCount
              << " descriptor set(s)\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets) {

    ICD_LOG_INFO() << "[Client ICD] vkFreeDescriptorSets called (count=" << descriptorSetCount << ")\n";

    if (descriptorSetCount == 0) {
        return VK_SUCCESS;
    }
    if (!pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkFreeDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSet> remote_sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        remote_sets[i] = g_pipeline_state.get_remote_descriptor_set(pDescriptorSets[i]);
        if (remote_sets[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked during free\n";
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
        ICD_LOG_INFO() << "[Client ICD] Freed " << descriptorSetCount << " descriptor set(s)\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkFreeDescriptorSets failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet* pDescriptorCopies) {

    ICD_LOG_INFO() << "[Client ICD] vkUpdateDescriptorSets called (writes=" << descriptorWriteCount
              << ", copies=" << descriptorCopyCount << ")\n";

    if (descriptorWriteCount == 0 && descriptorCopyCount == 0) {
        return;
    }

    if ((!pDescriptorWrites && descriptorWriteCount > 0) ||
        (!pDescriptorCopies && descriptorCopyCount > 0)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid descriptor write/copy arrays\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkUpdateDescriptorSets\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkWriteDescriptorSet> remote_writes(descriptorWriteCount);
    std::vector<std::vector<VkDescriptorBufferInfo>> buffer_infos(descriptorWriteCount);
    std::vector<std::vector<VkDescriptorImageInfo>> image_infos(descriptorWriteCount);
    std::vector<std::vector<VkBufferView>> texel_views(descriptorWriteCount);
    std::vector<VkWriteDescriptorSet> pending_writes;
    pending_writes.reserve(descriptorWriteCount);

    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet& src = pDescriptorWrites[i];
        VkWriteDescriptorSet& dst = remote_writes[i];
        dst = src;
        dst.dstSet = g_pipeline_state.get_remote_descriptor_set(src.dstSet);
        if (dst.dstSet == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked in vkUpdateDescriptorSets\n";
            return;
        }

        // Log what's being updated
        const char* desc_type_str = "UNKNOWN";
        switch (src.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: desc_type_str = "UNIFORM_BUFFER"; break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: desc_type_str = "STORAGE_BUFFER"; break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: desc_type_str = "UNIFORM_BUFFER_DYNAMIC"; break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: desc_type_str = "STORAGE_BUFFER_DYNAMIC"; break;
        case VK_DESCRIPTOR_TYPE_SAMPLER: desc_type_str = "SAMPLER"; break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: desc_type_str = "COMBINED_IMAGE_SAMPLER"; break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: desc_type_str = "SAMPLED_IMAGE"; break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: desc_type_str = "STORAGE_IMAGE"; break;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: desc_type_str = "INPUT_ATTACHMENT"; break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: desc_type_str = "UNIFORM_TEXEL_BUFFER"; break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: desc_type_str = "STORAGE_TEXEL_BUFFER"; break;
        default: break;
        }
        ICD_LOG_INFO() << "  [" << i << "] Type=" << desc_type_str
                       << ", Binding=" << src.dstBinding
                       << ", Count=" << src.descriptorCount
                       << ", ArrayElem=" << src.dstArrayElement << "\n";

        switch (src.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!src.pBufferInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing buffer info for descriptor update\n";
                return;
            }
            buffer_infos[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                buffer_infos[i][j] = src.pBufferInfo[j];
                if (buffer_infos[i][j].buffer != VK_NULL_HANDLE) {
                    buffer_infos[i][j].buffer =
                        g_resource_state.get_remote_buffer(src.pBufferInfo[j].buffer);
                    if (buffer_infos[i][j].buffer == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            dst.pBufferInfo = buffer_infos[i].data();
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (!src.pImageInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing image info for descriptor update\n";
                return;
            }
            image_infos[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                image_infos[i][j] = src.pImageInfo[j];
                if (image_infos[i][j].imageView != VK_NULL_HANDLE) {
                    image_infos[i][j].imageView =
                        g_resource_state.get_remote_image_view(src.pImageInfo[j].imageView);
                    if (image_infos[i][j].imageView == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Image view not tracked for descriptor update\n";
                        return;
                    }
                }
                if (image_infos[i][j].sampler != VK_NULL_HANDLE) {
                    image_infos[i][j].sampler =
                        g_resource_state.get_remote_sampler(src.pImageInfo[j].sampler);
                    if (image_infos[i][j].sampler == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Sampler not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = image_infos[i].data();
            dst.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!src.pTexelBufferView) {
                ICD_LOG_ERROR() << "[Client ICD] Missing texel buffer info for descriptor update\n";
                return;
            }
            texel_views[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                if (src.pTexelBufferView[j] == VK_NULL_HANDLE) {
                    texel_views[i][j] = VK_NULL_HANDLE;
                    continue;
                }
                texel_views[i][j] =
                    g_resource_state.get_remote_buffer_view(src.pTexelBufferView[j]);
                if (texel_views[i][j] == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] Buffer view not tracked for descriptor update\n";
                    return;
                }
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = texel_views[i].data();
            break;
        default:
            if (src.descriptorCount > 0) {
                ICD_LOG_ERROR() << "[Client ICD] Unsupported descriptor type in vkUpdateDescriptorSets\n";
                return;
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = nullptr;
            break;
        }

        const VkDescriptorBufferInfo* buffer_ptr = buffer_infos[i].empty() ? nullptr : buffer_infos[i].data();
        const VkDescriptorImageInfo* image_ptr = image_infos[i].empty() ? nullptr : image_infos[i].data();
        const VkBufferView* texel_ptr = texel_views[i].empty() ? nullptr : texel_views[i].data();

        // Only send updates when something actually changed to cut network traffic.
        bool changed = g_pipeline_state.update_descriptor_write_cache(src.dstSet,
                                                                      dst,
                                                                      buffer_ptr,
                                                                      image_ptr,
                                                                      texel_ptr);
        if (!changed) {
            ICD_LOG_INFO() << "  [" << i << "] Skipping unchanged descriptor write" << "\n";
            continue;
        }

        VENUS_PROFILE_DESCRIPTOR_TYPE(src.descriptorType);
        pending_writes.push_back(dst);
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
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked for copy update\n";
            return;
        }
    }

    if (pending_writes.empty() && descriptorCopyCount == 0) {
        ICD_LOG_INFO() << "[Client ICD] Descriptor updates skipped (no changes)\n";
        return;
    }

    vn_async_vkUpdateDescriptorSets(&g_ring,
                                    icd_device->remote_handle,
                                    static_cast<uint32_t>(pending_writes.size()),
                                    pending_writes.empty() ? nullptr : pending_writes.data(),
                                    descriptorCopyCount,
                                    remote_copies.data());
    ICD_LOG_INFO() << "[Client ICD] Descriptor sets updated (writes sent="
              << pending_writes.size() << "/" << descriptorWriteCount
              << ", copies=" << descriptorCopyCount << ")\n";
}

} // extern "C"
