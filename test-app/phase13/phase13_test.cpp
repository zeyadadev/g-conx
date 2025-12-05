#include "phase13_test.h"

#include "logging.h"
#include <vulkan/vulkan.h>

#include <cstring>
#include <vector>

namespace {

struct DeviceBundle {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
};

struct BufferResources {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

uint32_t find_memory_type(uint32_t type_bits,
                          VkMemoryPropertyFlags desired,
                          const VkPhysicalDeviceMemoryProperties& props) {
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & desired) == desired) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool pick_physical(DeviceBundle* bundle) {
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(bundle->instance, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(bundle->instance, &count, devices.data()) != VK_SUCCESS) {
        return false;
    }
    bundle->physical = devices[0];
    return true;
}

bool pick_queue_family(DeviceBundle* bundle) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(bundle->physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(bundle->physical, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            bundle->queue_family = i;
            return true;
        }
    }
    return false;
}

bool create_device(DeviceBundle* bundle,
                   bool enable_line_stipple,
                   bool enable_maintenance5,
                   bool enable_maintenance6,
                   const VkPhysicalDeviceMaintenance5FeaturesKHR& maint5_feats,
                   const VkPhysicalDeviceMaintenance6FeaturesKHR& maint6_feats,
                   const VkPhysicalDeviceLineRasterizationFeaturesEXT& line_feats) {
    if (!pick_queue_family(bundle)) {
        TEST_LOG_WARN() << "⚠️ No graphics queue family, skipping";
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = bundle->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceLineRasterizationFeaturesEXT line_feats_enable = line_feats;
    if (!enable_line_stipple) {
        line_feats_enable.stippledRectangularLines = VK_FALSE;
        line_feats_enable.stippledBresenhamLines = VK_FALSE;
        line_feats_enable.stippledSmoothLines = VK_FALSE;
    }

    VkPhysicalDeviceMaintenance6FeaturesKHR maint6_enable = maint6_feats;
    maint6_enable.maintenance6 = enable_maintenance6 ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceMaintenance5FeaturesKHR maint5_enable = maint5_feats;
    maint5_enable.maintenance5 = enable_maintenance5 ? VK_TRUE : VK_FALSE;
    maint5_enable.pNext = &maint6_enable;
    maint6_enable.pNext = &line_feats_enable;

    VkPhysicalDeviceFeatures2 feats2 = {};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &maint5_enable;

    std::vector<const char*> enabled_exts;
    VkDeviceCreateInfo dev_info = {};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.pNext = &feats2;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &queue_info;
    dev_info.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
    dev_info.ppEnabledExtensionNames = enabled_exts.empty() ? nullptr : enabled_exts.data();

    VkResult res = vkCreateDevice(bundle->physical, &dev_info, nullptr, &bundle->device);
    if (res != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << res;
        return false;
    }

    vkGetDeviceQueue(bundle->device, bundle->queue_family, 0, &bundle->queue);
    if (!bundle->queue) {
        TEST_LOG_ERROR() << "✗ vkGetDeviceQueue returned NULL";
        return false;
    }
    return true;
}

bool create_command_buffer(const DeviceBundle& bundle,
                           VkCommandPool* pool_out,
                           VkCommandBuffer* cb_out) {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = bundle.queue_family;
    if (vkCreateCommandPool(bundle.device, &pool_info, nullptr, pool_out) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create command pool";
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = *pool_out;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(bundle.device, &alloc_info, cb_out) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to allocate command buffer";
        return false;
    }
    return true;
}

bool create_buffer(const DeviceBundle& bundle,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   uint32_t memory_type,
                   BufferResources* out) {
    VkBufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(bundle.device, &info, nullptr, &out->buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create buffer";
        return false;
    }

    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(bundle.device, out->buffer, &reqs);

    VkMemoryAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = reqs.size;
    alloc.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(bundle.device, &alloc, nullptr, &out->memory) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to allocate buffer memory";
        return false;
    }
    if (vkBindBufferMemory(bundle.device, out->buffer, out->memory, 0) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to bind buffer memory";
        return false;
    }
    out->size = reqs.size;
    return true;
}

void destroy_buffer(const DeviceBundle& bundle, BufferResources* buf) {
    if (buf->buffer) {
        vkDestroyBuffer(bundle.device, buf->buffer, nullptr);
        buf->buffer = VK_NULL_HANDLE;
    }
    if (buf->memory) {
        vkFreeMemory(bundle.device, buf->memory, nullptr);
        buf->memory = VK_NULL_HANDLE;
    }
}

bool create_descriptor_template(const DeviceBundle& bundle,
                                VkPipelineLayout pipeline_layout,
                                VkDescriptorSetLayout set_layout,
                                VkDescriptorUpdateTemplate* tmpl_out) {
    VkDescriptorUpdateTemplateCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
    info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
    info.descriptorSetLayout = set_layout;
    info.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    info.pipelineLayout = pipeline_layout;
    info.set = 0;
    info.descriptorUpdateEntryCount = 0;
    info.pDescriptorUpdateEntries = nullptr;

    if (vkCreateDescriptorUpdateTemplate(bundle.device, &info, nullptr, tmpl_out) != VK_SUCCESS) {
        TEST_LOG_WARN() << "⚠️ Failed to create descriptor update template, skipping template push";
        *tmpl_out = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace

bool run_phase13_test() {
    TEST_LOG_INFO() << "\n========================================";
    TEST_LOG_INFO() << "Phase 13: Vulkan 1.4 Command Coverage";
    TEST_LOG_INFO() << "========================================\n";

    DeviceBundle dev = {};
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    BufferResources index_buffer;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate desc_template = VK_NULL_HANDLE;
    VkDeviceMemory map_memory = VK_NULL_HANDLE;
    bool success = false;

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Phase 13";
    app.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo inst_info = {};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pApplicationInfo = &app;

    VkResult res = vkCreateInstance(&inst_info, nullptr, &dev.instance);
    if (res != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed: " << res;
        return false;
    }

    do {
        if (!pick_physical(&dev)) {
            TEST_LOG_ERROR() << "✗ Failed to pick physical device";
            break;
        }

        VkPhysicalDeviceMaintenance5FeaturesKHR maint5_feats = {};
        maint5_feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR;
        VkPhysicalDeviceMaintenance6FeaturesKHR maint6_feats = {};
        maint6_feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR;
        VkPhysicalDeviceLineRasterizationFeaturesEXT line_feats = {};
        line_feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
        maint5_feats.pNext = &maint6_feats;
        maint6_feats.pNext = &line_feats;

        VkPhysicalDeviceFeatures2 feats2 = {};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.pNext = &maint5_feats;
        vkGetPhysicalDeviceFeatures2(dev.physical, &feats2);

        const bool support_maint5 = maint5_feats.maintenance5 == VK_TRUE;
        const bool support_maint6 = maint6_feats.maintenance6 == VK_TRUE;
        const bool support_line_stipple = (line_feats.stippledRectangularLines == VK_TRUE) ||
                                          (line_feats.stippledBresenhamLines == VK_TRUE) ||
                                          (line_feats.stippledSmoothLines == VK_TRUE);

        if (!create_device(&dev,
                           support_line_stipple,
                           support_maint5,
                           support_maint6,
                           maint5_feats,
                           maint6_feats,
                           line_feats)) {
            break;
        }

        VkPhysicalDeviceMemoryProperties mem_props = {};
        vkGetPhysicalDeviceMemoryProperties(dev.physical, &mem_props);

        uint32_t host_visible_type =
            find_memory_type(0xffffffffu, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, mem_props);
        VkMemoryAllocateInfo map_alloc = {};
        if (host_visible_type != UINT32_MAX) {
            map_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            map_alloc.allocationSize = 4096;
            map_alloc.memoryTypeIndex = host_visible_type;
            res = vkAllocateMemory(dev.device, &map_alloc, nullptr, &map_memory);
            if (res == VK_SUCCESS) {
                VkMemoryMapInfo map_info = {};
                map_info.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO;
                map_info.memory = map_memory;
                map_info.offset = 0;
                map_info.size = map_alloc.allocationSize;
                map_info.flags = 0;
                void* ptr = nullptr;
                res = vkMapMemory2(dev.device, &map_info, &ptr);
                if (res != VK_SUCCESS) {
                    TEST_LOG_ERROR() << "✗ vkMapMemory2 failed: " << res;
                    break;
                }
                if (ptr) {
                    std::memset(ptr, 0xaa, 16);
                }
                VkMemoryUnmapInfo unmap_info = {};
                unmap_info.sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO;
                unmap_info.memory = map_memory;
                unmap_info.flags = 0;
                res = vkUnmapMemory2(dev.device, &unmap_info);
                if (res != VK_SUCCESS) {
                    TEST_LOG_ERROR() << "✗ vkUnmapMemory2 failed: " << res;
                    break;
                }
            } else {
                TEST_LOG_WARN() << "⚠️ vkAllocateMemory for map/unmap2 failed, skipping map test";
            }
        } else {
            TEST_LOG_WARN() << "⚠️ No HOST_VISIBLE memory type, skipping map/unmap2";
        }

        if (!create_command_buffer(dev, &cmd_pool, &cmd)) {
            break;
        }

        VkDescriptorSetLayoutCreateInfo set_info = {};
        set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set_info.bindingCount = 0;
        if (vkCreateDescriptorSetLayout(dev.device, &set_info, nullptr, &set_layout) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ Failed to create descriptor set layout";
            break;
        }

        VkPushConstantRange range = {};
        range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        range.offset = 0;
        range.size = sizeof(uint32_t);

        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(dev.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ Failed to create pipeline layout";
            break;
        }

        // Create index buffer for vkCmdBindIndexBuffer2
        uint32_t buffer_memory_type = UINT32_MAX;
        VkBufferCreateInfo temp_info = {};
        temp_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        temp_info.size = 256;
        temp_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        temp_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer temp_buffer = VK_NULL_HANDLE;
        vkCreateBuffer(dev.device, &temp_info, nullptr, &temp_buffer);
        VkMemoryRequirements temp_reqs = {};
        vkGetBufferMemoryRequirements(dev.device, temp_buffer, &temp_reqs);
        buffer_memory_type = find_memory_type(temp_reqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              mem_props);
        vkDestroyBuffer(dev.device, temp_buffer, nullptr);
        if (buffer_memory_type == UINT32_MAX) {
            buffer_memory_type = find_memory_type(temp_reqs.memoryTypeBits, 0, mem_props);
        }
        if (buffer_memory_type == UINT32_MAX) {
            TEST_LOG_WARN() << "⚠️ No usable memory type for index buffer, skipping buffer-based tests";
        } else {
            if (!create_buffer(dev,
                               256,
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               buffer_memory_type,
                               &index_buffer)) {
                TEST_LOG_ERROR() << "✗ Failed to set up index buffer";
                break;
            }
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        if (support_maint5 && index_buffer.buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer2(cmd, index_buffer.buffer, 0, index_buffer.size, VK_INDEX_TYPE_UINT16);
        } else {
            TEST_LOG_WARN() << "⚠️ Maintenance5 feature not present or buffer missing; skipping vkCmdBindIndexBuffer2";
        }

        if (support_maint6) {
            VkBindDescriptorSetsInfo bind_info = {};
            bind_info.sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO;
            bind_info.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            bind_info.layout = pipeline_layout;
            bind_info.firstSet = 0;
            bind_info.descriptorSetCount = 0;
            bind_info.pDescriptorSets = nullptr;
            bind_info.dynamicOffsetCount = 0;
            bind_info.pDynamicOffsets = nullptr;
            vkCmdBindDescriptorSets2(cmd, &bind_info);

            uint32_t push_value = 0x12345678;
            VkPushConstantsInfo push_info = {};
            push_info.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
            push_info.layout = pipeline_layout;
            push_info.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            push_info.offset = 0;
            push_info.size = sizeof(uint32_t);
            push_info.pValues = &push_value;
            vkCmdPushConstants2(cmd, &push_info);

            VkPushDescriptorSetInfo push_desc = {};
            push_desc.sType = VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_INFO;
            push_desc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            push_desc.layout = pipeline_layout;
            push_desc.set = 0;
            push_desc.descriptorWriteCount = 0;
            push_desc.pDescriptorWrites = nullptr;
            vkCmdPushDescriptorSet2(cmd, &push_desc);

            if (create_descriptor_template(dev, pipeline_layout, set_layout, &desc_template) &&
                desc_template != VK_NULL_HANDLE) {
                VkPushDescriptorSetWithTemplateInfo push_tmpl = {};
                push_tmpl.sType = VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_INFO;
                push_tmpl.descriptorUpdateTemplate = desc_template;
                push_tmpl.layout = pipeline_layout;
                push_tmpl.set = 0;
                push_tmpl.pData = nullptr;
                vkCmdPushDescriptorSetWithTemplate2(cmd, &push_tmpl);
            } else {
                TEST_LOG_WARN() << "⚠️ Descriptor update template unavailable, skipping template push";
            }

            VkRenderingAttachmentLocationInfo loc_info = {};
            loc_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
            loc_info.colorAttachmentCount = 0;
            loc_info.pColorAttachmentLocations = nullptr;
            vkCmdSetRenderingAttachmentLocations(cmd, &loc_info);
        } else {
            TEST_LOG_WARN() << "⚠️ Maintenance6 feature not present; skipping descriptor/push/rendering-location commands";
        }

        if (support_line_stipple) {
            vkCmdSetLineStipple(cmd, 1, 0xffff);
        } else {
            TEST_LOG_WARN() << "⚠️ Line stipple not supported; skipping vkCmdSetLineStipple";
        }

        vkEndCommandBuffer(cmd);
        success = true;
    } while (false);

    if (desc_template) {
        vkDestroyDescriptorUpdateTemplate(dev.device, desc_template, nullptr);
    }
    if (pipeline_layout) {
        vkDestroyPipelineLayout(dev.device, pipeline_layout, nullptr);
    }
    if (set_layout) {
        vkDestroyDescriptorSetLayout(dev.device, set_layout, nullptr);
    }
    destroy_buffer(dev, &index_buffer);
    if (map_memory) {
        vkFreeMemory(dev.device, map_memory, nullptr);
    }
    if (cmd_pool) {
        vkDestroyCommandPool(dev.device, cmd_pool, nullptr);
    }
    if (dev.device) {
        vkDeviceWaitIdle(dev.device);
        vkDestroyDevice(dev.device, nullptr);
    }
    if (dev.instance) {
        vkDestroyInstance(dev.instance, nullptr);
    }

    if (success) {
        TEST_LOG_INFO() << "Phase 13 PASSED";
    }
    return success;
}
