// Descriptor Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "profiling.h"

#include <unordered_map>
#include <mutex>

namespace {

// Cached push descriptor limits keyed by physical device handle.
uint32_t get_push_descriptor_limit(VkDevice device) {
    DeviceEntry* entry = g_device_state.get_device(device);
    if (!entry) {
        return 0;
    }
    VkPhysicalDevice phys_dev = entry->physical_device;
    if (phys_dev == VK_NULL_HANDLE) {
        return 0;
    }

    static std::mutex cache_mutex;
    static std::unordered_map<uint64_t, uint32_t> cache;

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(reinterpret_cast<uint64_t>(phys_dev));
        if (it != cache.end()) {
            return it->second;
        }
    }

    VkPhysicalDevicePushDescriptorPropertiesKHR push_props = {};
    push_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &push_props;
    vkGetPhysicalDeviceProperties2(phys_dev, &props2);

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[reinterpret_cast<uint64_t>(phys_dev)] = push_props.maxPushDescriptors;
    }
    return push_props.maxPushDescriptors;
}

bool device_supports_push_descriptors(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return true;
    }
    if (g_device_state.is_extension_enabled(device, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
        return true;
    }
    const auto* vk14_features = g_device_state.get_vk14_features(device);
    return vk14_features && vk14_features->pushDescriptor;
}

size_t compute_template_data_size(const std::vector<VkDescriptorUpdateTemplateEntry>& entries) {
    size_t max_offset = 0;
    for (const auto& entry : entries) {
        size_t end = static_cast<size_t>(entry.offset) +
                     static_cast<size_t>(entry.stride) * entry.descriptorCount;
        if (end > max_offset) {
            max_offset = end;
        }
    }
    return max_offset;
}

struct PreparedWrite {
    VkWriteDescriptorSet write = {};
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkBufferView> texel_views;
};

bool build_writes_from_template_data(const DescriptorUpdateTemplateInfo& tmpl_info,
                                     VkDescriptorSet dst_set,
                                     const void* pData,
                                     std::vector<PreparedWrite>* out_writes) {
    if (!out_writes) {
        return false;
    }
    out_writes->clear();

    const size_t data_size = compute_template_data_size(tmpl_info.entries);
    if (data_size > 0 && !pData) {
        ICD_LOG_ERROR() << "[Client ICD] Template data is NULL but size is non-zero\n";
        return false;
    }

    const uint8_t* data_bytes = static_cast<const uint8_t*>(pData);

    for (const auto& entry : tmpl_info.entries) {
        PreparedWrite prepared = {};
        prepared.write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        prepared.write.pNext = nullptr;
        prepared.write.dstSet = dst_set;
        prepared.write.dstBinding = entry.dstBinding;
        prepared.write.dstArrayElement = entry.dstArrayElement;
        prepared.write.descriptorCount = entry.descriptorCount;
        prepared.write.descriptorType = entry.descriptorType;

        for (uint32_t i = 0; i < entry.descriptorCount; ++i) {
            size_t offset = static_cast<size_t>(entry.offset) + static_cast<size_t>(entry.stride) * i;
            const uint8_t* element_ptr = data_bytes ? data_bytes + offset : nullptr;

            switch (entry.descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                if (!element_ptr) {
                    ICD_LOG_ERROR() << "[Client ICD] Missing buffer info for template entry\n";
                    return false;
                }
                const VkDescriptorBufferInfo* src = reinterpret_cast<const VkDescriptorBufferInfo*>(element_ptr);
                VkDescriptorBufferInfo info = *src;
                if (info.buffer != VK_NULL_HANDLE) {
                    info.buffer = g_resource_state.get_remote_buffer(src->buffer);
                    if (info.buffer == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in template data\n";
                        return false;
                    }
                }
                prepared.buffers.push_back(info);
                break;
            }
            case VK_DESCRIPTOR_TYPE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                if (!element_ptr) {
                    ICD_LOG_ERROR() << "[Client ICD] Missing image info for template entry\n";
                    return false;
                }
                const VkDescriptorImageInfo* src = reinterpret_cast<const VkDescriptorImageInfo*>(element_ptr);
                VkDescriptorImageInfo info = *src;
                if (info.imageView != VK_NULL_HANDLE) {
                    info.imageView = g_resource_state.get_remote_image_view(src->imageView);
                    if (info.imageView == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Image view not tracked in template data\n";
                        return false;
                    }
                }
                if (info.sampler != VK_NULL_HANDLE) {
                    info.sampler = g_resource_state.get_remote_sampler(src->sampler);
                    if (info.sampler == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Sampler not tracked in template data\n";
                        return false;
                    }
                }
                prepared.images.push_back(info);
                break;
            }
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                if (!element_ptr) {
                    ICD_LOG_ERROR() << "[Client ICD] Missing texel buffer view for template entry\n";
                    return false;
                }
                const VkBufferView* src = reinterpret_cast<const VkBufferView*>(element_ptr);
                VkBufferView view = *src;
                if (view != VK_NULL_HANDLE) {
                    view = g_resource_state.get_remote_buffer_view(*src);
                    if (view == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer view not tracked in template data\n";
                        return false;
                    }
                }
                prepared.texel_views.push_back(view);
                break;
            }
            default:
                ICD_LOG_ERROR() << "[Client ICD] Unsupported descriptor type in template entry\n";
                return false;
            }
        }

        prepared.write.pBufferInfo =
            prepared.buffers.empty() ? nullptr : prepared.buffers.data();
        prepared.write.pImageInfo =
            prepared.images.empty() ? nullptr : prepared.images.data();
        prepared.write.pTexelBufferView =
            prepared.texel_views.empty() ? nullptr : prepared.texel_views.data();

        out_writes->push_back(std::move(prepared));
    }
    return true;
}

VkPipelineBindPoint infer_bind_point_from_stages(VkShaderStageFlags stage_flags) {
    return (stage_flags & VK_SHADER_STAGE_COMPUTE_BIT) ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                       : VK_PIPELINE_BIND_POINT_GRAPHICS;
}

} // namespace

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
    g_pipeline_state.add_descriptor_set_layout(device, local, remote_layout, pCreateInfo);
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
    struct PreparedWrite {
        VkWriteDescriptorSet write = {};
        std::vector<VkDescriptorBufferInfo> buffers;
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkBufferView> texel_views;
    };

    std::vector<PreparedWrite> prepared_writes;
    prepared_writes.reserve(descriptorWriteCount);

    struct BindingKey {
        VkDescriptorSet set;
        uint32_t binding;
        bool operator==(const BindingKey& other) const {
            return set == other.set && binding == other.binding;
        }
    };
    struct BindingKeyHash {
        std::size_t operator()(const BindingKey& k) const {
            // Combine handle value and binding index.
            return std::hash<uint64_t>()(reinterpret_cast<uint64_t>(k.set)) ^
                   (static_cast<std::size_t>(k.binding) << 1);
        }
    };

    std::unordered_map<BindingKey, size_t, BindingKeyHash> last_write_for_binding;

    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet& src = pDescriptorWrites[i];
        PreparedWrite current = {};
        current.write = src;
        current.write.dstSet = g_pipeline_state.get_remote_descriptor_set(src.dstSet);
        if (current.write.dstSet == VK_NULL_HANDLE) {
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
            current.buffers.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                current.buffers[j] = src.pBufferInfo[j];
                if (current.buffers[j].buffer != VK_NULL_HANDLE) {
                    current.buffers[j].buffer =
                        g_resource_state.get_remote_buffer(src.pBufferInfo[j].buffer);
                    if (current.buffers[j].buffer == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            current.write.pBufferInfo = current.buffers.data();
            current.write.pImageInfo = nullptr;
            current.write.pTexelBufferView = nullptr;
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
            current.images.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                current.images[j] = src.pImageInfo[j];
                if (current.images[j].imageView != VK_NULL_HANDLE) {
                    current.images[j].imageView =
                        g_resource_state.get_remote_image_view(src.pImageInfo[j].imageView);
                    if (current.images[j].imageView == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Image view not tracked for descriptor update\n";
                        return;
                    }
                }
                if (current.images[j].sampler != VK_NULL_HANDLE) {
                    current.images[j].sampler =
                        g_resource_state.get_remote_sampler(src.pImageInfo[j].sampler);
                    if (current.images[j].sampler == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Sampler not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            current.write.pBufferInfo = nullptr;
            current.write.pImageInfo = current.images.data();
            current.write.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!src.pTexelBufferView) {
                ICD_LOG_ERROR() << "[Client ICD] Missing texel buffer info for descriptor update\n";
                return;
            }
            current.texel_views.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                if (src.pTexelBufferView[j] == VK_NULL_HANDLE) {
                    current.texel_views[j] = VK_NULL_HANDLE;
                    continue;
                }
                current.texel_views[j] =
                    g_resource_state.get_remote_buffer_view(src.pTexelBufferView[j]);
                if (current.texel_views[j] == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] Buffer view not tracked for descriptor update\n";
                    return;
                }
            }
            current.write.pBufferInfo = nullptr;
            current.write.pImageInfo = nullptr;
            current.write.pTexelBufferView = current.texel_views.data();
            break;
        default:
            if (src.descriptorCount > 0) {
                ICD_LOG_ERROR() << "[Client ICD] Unsupported descriptor type in vkUpdateDescriptorSets\n";
                return;
            }
            current.write.pBufferInfo = nullptr;
            current.write.pImageInfo = nullptr;
            current.write.pTexelBufferView = nullptr;
            break;
        }

        const VkDescriptorBufferInfo* buffer_ptr = current.buffers.empty() ? nullptr : current.buffers.data();
        const VkDescriptorImageInfo* image_ptr = current.images.empty() ? nullptr : current.images.data();
        const VkBufferView* texel_ptr = current.texel_views.empty() ? nullptr : current.texel_views.data();

        // Only send updates when something actually changed to cut network traffic.
        bool changed = g_pipeline_state.update_descriptor_write_cache(src.dstSet,
                                                                      current.write,
                                                                      buffer_ptr,
                                                                      image_ptr,
                                                                      texel_ptr);
        if (!changed) {
            ICD_LOG_INFO() << "  [" << i << "] Skipping unchanged descriptor write" << "\n";
            continue;
        }

        VENUS_PROFILE_DESCRIPTOR_TYPE(src.descriptorType);
        BindingKey key{src.dstSet, src.dstBinding};
        auto it = last_write_for_binding.find(key);
        if (it == last_write_for_binding.end()) {
            last_write_for_binding.emplace(key, prepared_writes.size());
            prepared_writes.push_back(std::move(current));
        } else {
            prepared_writes[it->second] = std::move(current);  // Last write wins.
        }
    }

    std::vector<VkWriteDescriptorSet> pending_writes;
    pending_writes.reserve(prepared_writes.size());
    for (auto& prepared : prepared_writes) {
        pending_writes.push_back(prepared.write);
        VkWriteDescriptorSet& write = pending_writes.back();
        write.pBufferInfo = prepared.buffers.empty() ? nullptr : prepared.buffers.data();
        write.pImageInfo = prepared.images.empty() ? nullptr : prepared.images.data();
        write.pTexelBufferView = prepared.texel_views.empty() ? nullptr : prepared.texel_views.data();
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

// ===== Descriptor Update Template Functions =====

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDescriptorUpdateTemplate called\n";

    if (!pCreateInfo || !pDescriptorUpdateTemplate) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateDescriptorUpdateTemplate\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateDescriptorUpdateTemplate\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);

    // Translate descriptor set layout and pipeline layout to remote handles
    VkDescriptorUpdateTemplateCreateInfo remote_info = *pCreateInfo;
    if (pCreateInfo->descriptorSetLayout != VK_NULL_HANDLE) {
        remote_info.descriptorSetLayout =
            g_pipeline_state.get_remote_descriptor_set_layout(pCreateInfo->descriptorSetLayout);
        if (remote_info.descriptorSetLayout == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set layout not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    if (pCreateInfo->pipelineLayout != VK_NULL_HANDLE) {
        remote_info.pipelineLayout =
            g_pipeline_state.get_remote_pipeline_layout(pCreateInfo->pipelineLayout);
        if (remote_info.pipelineLayout == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkDescriptorUpdateTemplate remote_template = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateDescriptorUpdateTemplate(&g_ring,
                                                                icd_device->remote_handle,
                                                                &remote_info,
                                                                pAllocator,
                                                                &remote_template);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDescriptorUpdateTemplate failed: " << result << "\n";
        return result;
    }

    // Allocate local handle and store mapping
    VkDescriptorUpdateTemplate local = g_handle_allocator.allocate<VkDescriptorUpdateTemplate>();
    g_pipeline_state.add_descriptor_update_template(device, local, remote_template, pCreateInfo);

    *pDescriptorUpdateTemplate = local;
    ICD_LOG_INFO() << "[Client ICD] Descriptor update template created: local=" << local
                   << ", remote=" << remote_template << "\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(
    VkDevice device,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDescriptorUpdateTemplate called: " << descriptorUpdateTemplate << "\n";

    if (descriptorUpdateTemplate == VK_NULL_HANDLE) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyDescriptorUpdateTemplate\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDescriptorUpdateTemplate remote_template =
        g_pipeline_state.get_remote_descriptor_update_template(descriptorUpdateTemplate);

    if (remote_template == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Descriptor update template not tracked\n";
        return;
    }

    vn_async_vkDestroyDescriptorUpdateTemplate(&g_ring,
                                                icd_device->remote_handle,
                                                remote_template,
                                                pAllocator);

    g_pipeline_state.remove_descriptor_update_template(device, descriptorUpdateTemplate);
    ICD_LOG_INFO() << "[Client ICD] Descriptor update template destroyed\n";
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice device,
    VkDescriptorSet descriptorSet,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const void* pData) {

    ICD_LOG_INFO() << "[Client ICD] vkUpdateDescriptorSetWithTemplate called\n";

    if (descriptorSet == VK_NULL_HANDLE || descriptorUpdateTemplate == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Null handles passed to vkUpdateDescriptorSetWithTemplate\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkUpdateDescriptorSetWithTemplate\n";
        return;
    }

    VkDescriptorSet remote_set = g_pipeline_state.get_remote_descriptor_set(descriptorSet);
    VkDescriptorUpdateTemplate remote_template =
        g_pipeline_state.get_remote_descriptor_update_template(descriptorUpdateTemplate);
    DescriptorUpdateTemplateInfo tmpl_info = {};
    if (!g_pipeline_state.get_descriptor_update_template_info(descriptorUpdateTemplate, &tmpl_info)) {
        ICD_LOG_ERROR() << "[Client ICD] Descriptor update template metadata not found\n";
        return;
    }

    if (remote_set == VK_NULL_HANDLE || remote_template == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor handles missing for template update\n";
        return;
    }

    if (tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) {
        ICD_LOG_ERROR() << "[Client ICD] Unsupported template type for vkUpdateDescriptorSetWithTemplate\n";
        return;
    }

    std::vector<PreparedWrite> prepared;
    if (!build_writes_from_template_data(tmpl_info, remote_set, pData, &prepared)) {
        return;
    }

    if (prepared.empty()) {
        ICD_LOG_INFO() << "[Client ICD] Template update has no entries\n";
        return;
    }

    std::vector<VkWriteDescriptorSet> pending_writes;
    pending_writes.reserve(prepared.size());
    for (auto& write : prepared) {
        const VkDescriptorBufferInfo* buffer_ptr = write.buffers.empty() ? nullptr : write.buffers.data();
        const VkDescriptorImageInfo* image_ptr = write.images.empty() ? nullptr : write.images.data();
        const VkBufferView* texel_ptr = write.texel_views.empty() ? nullptr : write.texel_views.data();

        bool changed = g_pipeline_state.update_descriptor_write_cache(descriptorSet,
                                                                      write.write,
                                                                      buffer_ptr,
                                                                      image_ptr,
                                                                      texel_ptr);
        if (!changed) {
            continue;
        }

        pending_writes.push_back(write.write);
        VkWriteDescriptorSet& pending = pending_writes.back();
        pending.pBufferInfo = buffer_ptr;
        pending.pImageInfo = image_ptr;
        pending.pTexelBufferView = texel_ptr;
    }

    if (pending_writes.empty()) {
        ICD_LOG_INFO() << "[Client ICD] Template update skipped (no changes)\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkUpdateDescriptorSets(&g_ring,
                                    icd_device->remote_handle,
                                    static_cast<uint32_t>(pending_writes.size()),
                                    pending_writes.data(),
                                    0,
                                    nullptr);
    ICD_LOG_INFO() << "[Client ICD] Template descriptor update applied (" << pending_writes.size()
              << " writes)\n";
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t set,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPushDescriptorSet called (writes=" << descriptorWriteCount << ")\n";

    if (descriptorWriteCount == 0) {
        return;
    }
    if (!pDescriptorWrites) {
        ICD_LOG_ERROR() << "[Client ICD] pDescriptorWrites is NULL\n";
        return;
    }
    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPushDescriptorSet")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkDevice device = g_command_buffer_state.get_buffer_device(commandBuffer);
    if (!device_supports_push_descriptors(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptors not enabled on this device\n";
        return;
    }

    if (set != 0) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptors only support set 0\n";
        return;
    }

    uint32_t total_descriptors = 0;
    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        total_descriptors += pDescriptorWrites[i].descriptorCount;
    }
    const uint32_t max_push = get_push_descriptor_limit(device);
    if (max_push > 0 && total_descriptors > max_push) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptor count " << total_descriptors
                  << " exceeds device limit " << max_push << "\n";
        return;
    }

    VkCommandBuffer remote_cmd = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cmd == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer not tracked\n";
        return;
    }

    VkPipelineLayout remote_layout = g_pipeline_state.get_remote_pipeline_layout(layout);
    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked for push descriptors\n";
        return;
    }

    std::vector<PreparedWrite> prepared_writes;
    prepared_writes.reserve(descriptorWriteCount);

    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet& src = pDescriptorWrites[i];
        PreparedWrite current = {};
        current.write = src;
        current.write.dstSet = VK_NULL_HANDLE; // Unused for push descriptors

        switch (src.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!src.pBufferInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing buffer info for push descriptor write\n";
                return;
            }
            current.buffers.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                current.buffers[j] = src.pBufferInfo[j];
                if (current.buffers[j].buffer != VK_NULL_HANDLE) {
                    current.buffers[j].buffer =
                        g_resource_state.get_remote_buffer(src.pBufferInfo[j].buffer);
                    if (current.buffers[j].buffer == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked for push descriptor\n";
                        return;
                    }
                }
            }
            current.write.pBufferInfo = current.buffers.data();
            current.write.pImageInfo = nullptr;
            current.write.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (!src.pImageInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing image info for push descriptor write\n";
                return;
            }
            current.images.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                current.images[j] = src.pImageInfo[j];
                if (current.images[j].imageView != VK_NULL_HANDLE) {
                    current.images[j].imageView =
                        g_resource_state.get_remote_image_view(src.pImageInfo[j].imageView);
                    if (current.images[j].imageView == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Image view not tracked for push descriptor\n";
                        return;
                    }
                }
                if (current.images[j].sampler != VK_NULL_HANDLE) {
                    current.images[j].sampler =
                        g_resource_state.get_remote_sampler(src.pImageInfo[j].sampler);
                    if (current.images[j].sampler == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Sampler not tracked for push descriptor\n";
                        return;
                    }
                }
            }
            current.write.pBufferInfo = nullptr;
            current.write.pImageInfo = current.images.data();
            current.write.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!src.pTexelBufferView) {
                ICD_LOG_ERROR() << "[Client ICD] Missing texel buffer view for push descriptor write\n";
                return;
            }
            current.texel_views.resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                if (src.pTexelBufferView[j] == VK_NULL_HANDLE) {
                    current.texel_views[j] = VK_NULL_HANDLE;
                    continue;
                }
                current.texel_views[j] =
                    g_resource_state.get_remote_buffer_view(src.pTexelBufferView[j]);
                if (current.texel_views[j] == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] Buffer view not tracked for push descriptor\n";
                    return;
                }
            }
            current.write.pBufferInfo = nullptr;
            current.write.pImageInfo = nullptr;
            current.write.pTexelBufferView = current.texel_views.data();
            break;
        default:
            ICD_LOG_ERROR() << "[Client ICD] Unsupported descriptor type in push descriptor write\n";
            return;
        }

        prepared_writes.push_back(std::move(current));
    }

    std::vector<VkWriteDescriptorSet> pending;
    pending.reserve(prepared_writes.size());
    for (auto& prepared : prepared_writes) {
        pending.push_back(prepared.write);
        VkWriteDescriptorSet& write = pending.back();
        write.pBufferInfo = prepared.buffers.empty() ? nullptr : prepared.buffers.data();
        write.pImageInfo = prepared.images.empty() ? nullptr : prepared.images.data();
        write.pTexelBufferView = prepared.texel_views.empty() ? nullptr : prepared.texel_views.data();
    }

    vn_async_vkCmdPushDescriptorSet(&g_ring,
                                    remote_cmd,
                                    pipelineBindPoint,
                                    remote_layout,
                                    set,
                                    static_cast<uint32_t>(pending.size()),
                                    pending.data());
    ICD_LOG_INFO() << "[Client ICD] Pushed " << pending.size() << " descriptor writes\n";
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetKHR(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t set,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites) {
    vkCmdPushDescriptorSet(commandBuffer,
                           pipelineBindPoint,
                           layout,
                           set,
                           descriptorWriteCount,
                           pDescriptorWrites);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(
    VkCommandBuffer commandBuffer,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    VkPipelineLayout layout,
    uint32_t set,
    const void* pData) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPushDescriptorSetWithTemplate called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPushDescriptorSetWithTemplate")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkDevice device = g_command_buffer_state.get_buffer_device(commandBuffer);
    if (!device_supports_push_descriptors(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptors not enabled on this device\n";
        return;
    }

    if (set != 0) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptors only support set 0\n";
        return;
    }

    VkCommandBuffer remote_cmd = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cmd == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer not tracked\n";
        return;
    }

    VkPipelineLayout remote_layout = g_pipeline_state.get_remote_pipeline_layout(layout);
    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked for push descriptors\n";
        return;
    }

    VkDescriptorUpdateTemplate remote_template =
        g_pipeline_state.get_remote_descriptor_update_template(descriptorUpdateTemplate);
    DescriptorUpdateTemplateInfo tmpl_info = {};
    if (!g_pipeline_state.get_descriptor_update_template_info(descriptorUpdateTemplate, &tmpl_info)) {
        ICD_LOG_ERROR() << "[Client ICD] Descriptor update template metadata not found\n";
        return;
    }

    if (remote_template == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor update template handle missing\n";
        return;
    }
(void)remote_template;

    if (tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET &&
        tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR &&
        tmpl_info.template_type != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS) {
        ICD_LOG_ERROR() << "[Client ICD] Unsupported template type for push descriptors\n";
        return;
    }

    if (tmpl_info.set_number != set) {
        ICD_LOG_ERROR() << "[Client ICD] Template set index " << tmpl_info.set_number
                  << " does not match requested set " << set << "\n";
        return;
    }

    if (tmpl_info.pipeline_layout != VK_NULL_HANDLE && tmpl_info.pipeline_layout != layout) {
        ICD_LOG_WARN() << "[Client ICD] Pipeline layout differs from template definition; continuing\n";
    }

    uint32_t total_descriptors = 0;
    for (const auto& entry : tmpl_info.entries) {
        total_descriptors += entry.descriptorCount;
    }
    const uint32_t max_push = get_push_descriptor_limit(device);
    if (max_push > 0 && total_descriptors > max_push) {
        ICD_LOG_ERROR() << "[Client ICD] Push descriptor count " << total_descriptors
                  << " exceeds device limit " << max_push << "\n";
        return;
    }

    std::vector<PreparedWrite> prepared;
    if (!build_writes_from_template_data(tmpl_info, VK_NULL_HANDLE, pData, &prepared)) {
        return;
    }

    std::vector<VkWriteDescriptorSet> pending;
    pending.reserve(prepared.size());
    for (auto& write : prepared) {
        pending.push_back(write.write);
        VkWriteDescriptorSet& pending_write = pending.back();
        pending_write.pBufferInfo = write.buffers.empty() ? nullptr : write.buffers.data();
        pending_write.pImageInfo = write.images.empty() ? nullptr : write.images.data();
        pending_write.pTexelBufferView = write.texel_views.empty() ? nullptr : write.texel_views.data();
    }

    vn_async_vkCmdPushDescriptorSet(&g_ring,
                                    remote_cmd,
                                    tmpl_info.bind_point,
                                    remote_layout,
                                    set,
                                    static_cast<uint32_t>(pending.size()),
                                    pending.data());
    ICD_LOG_INFO() << "[Client ICD] Pushed " << pending.size()
              << " descriptor writes via template\n";
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer commandBuffer,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    VkPipelineLayout layout,
    uint32_t set,
    const void* pData) {
    vkCmdPushDescriptorSetWithTemplate(commandBuffer,
                                       descriptorUpdateTemplate,
                                       layout,
                                       set,
                                       pData);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(
    VkCommandBuffer commandBuffer,
    const VkPushDescriptorSetInfo* pPushDescriptorSetInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPushDescriptorSet2 called\n";

    if (!pPushDescriptorSetInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Missing VkPushDescriptorSetInfo\n";
        return;
    }

    VkPipelineBindPoint bind_point =
        infer_bind_point_from_stages(pPushDescriptorSetInfo->stageFlags);

    vkCmdPushDescriptorSet(commandBuffer,
                           bind_point,
                           pPushDescriptorSetInfo->layout,
                           pPushDescriptorSetInfo->set,
                           pPushDescriptorSetInfo->descriptorWriteCount,
                           pPushDescriptorSetInfo->pDescriptorWrites);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2KHR(
    VkCommandBuffer commandBuffer,
    const VkPushDescriptorSetInfo* pPushDescriptorSetInfo) {
    vkCmdPushDescriptorSet2(commandBuffer, pPushDescriptorSetInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(
    VkCommandBuffer commandBuffer,
    const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPushDescriptorSetWithTemplate2 called\n";

    if (!pPushDescriptorSetWithTemplateInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Missing VkPushDescriptorSetWithTemplateInfo\n";
        return;
    }

    vkCmdPushDescriptorSetWithTemplate(commandBuffer,
                                       pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate,
                                       pPushDescriptorSetWithTemplateInfo->layout,
                                       pPushDescriptorSetWithTemplateInfo->set,
                                       pPushDescriptorSetWithTemplateInfo->pData);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2KHR(
    VkCommandBuffer commandBuffer,
    const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo) {
    vkCmdPushDescriptorSetWithTemplate2(commandBuffer, pPushDescriptorSetWithTemplateInfo);
}

} // extern "C"
