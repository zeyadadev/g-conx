#include "phase09_test.h"

#include <cmath>
#include <cstring>
#include "logging.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace {

constexpr uint32_t kElementCount = 1024;
constexpr VkDeviceSize kBufferSize = kElementCount * sizeof(float);

static const uint32_t kSimpleAddSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x0000002c, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0006000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00060010, 0x00000004,
    0x00000011, 0x00000100, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000008, 0x00786469, 0x00080005, 0x0000000b,
    0x475f6c67, 0x61626f6c, 0x766e496c, 0x7461636f, 0x496e6f69, 0x00000044, 0x00040005, 0x00000012,
    0x7074754f, 0x00007475, 0x00040006, 0x00000012, 0x00000000, 0x00000063, 0x00030005, 0x00000014,
    0x00000000, 0x00040005, 0x00000019, 0x75706e49, 0x00004174, 0x00040006, 0x00000019, 0x00000000,
    0x00000061, 0x00030005, 0x0000001b, 0x00000000, 0x00040005, 0x00000021, 0x75706e49, 0x00004274,
    0x00040006, 0x00000021, 0x00000000, 0x00000062, 0x00030005, 0x00000023, 0x00000000, 0x00040047,
    0x0000000b, 0x0000000b, 0x0000001c, 0x00040047, 0x00000011, 0x00000006, 0x00000004, 0x00050048,
    0x00000012, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000012, 0x00000003, 0x00040047,
    0x00000014, 0x00000022, 0x00000000, 0x00040047, 0x00000014, 0x00000021, 0x00000002, 0x00040047,
    0x00000018, 0x00000006, 0x00000004, 0x00050048, 0x00000019, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x00000019, 0x00000003, 0x00040047, 0x0000001b, 0x00000022, 0x00000000, 0x00040047,
    0x0000001b, 0x00000021, 0x00000000, 0x00040047, 0x00000020, 0x00000006, 0x00000004, 0x00050048,
    0x00000021, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x00000021, 0x00000003, 0x00040047,
    0x00000023, 0x00000022, 0x00000000, 0x00040047, 0x00000023, 0x00000021, 0x00000001, 0x00040047,
    0x0000002b, 0x0000000b, 0x00000019, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00040015, 0x00000006, 0x00000020, 0x00000000, 0x00040020, 0x00000007, 0x00000007, 0x00000006,
    0x00040017, 0x00000009, 0x00000006, 0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000009,
    0x0004003b, 0x0000000a, 0x0000000b, 0x00000001, 0x0004002b, 0x00000006, 0x0000000c, 0x00000000,
    0x00040020, 0x0000000d, 0x00000001, 0x00000006, 0x00030016, 0x00000010, 0x00000020, 0x0003001d,
    0x00000011, 0x00000010, 0x0003001e, 0x00000012, 0x00000011, 0x00040020, 0x00000013, 0x00000002,
    0x00000012, 0x0004003b, 0x00000013, 0x00000014, 0x00000002, 0x00040015, 0x00000015, 0x00000020,
    0x00000001, 0x0004002b, 0x00000015, 0x00000016, 0x00000000, 0x0003001d, 0x00000018, 0x00000010,
    0x0003001e, 0x00000019, 0x00000018, 0x00040020, 0x0000001a, 0x00000002, 0x00000019, 0x0004003b,
    0x0000001a, 0x0000001b, 0x00000002, 0x00040020, 0x0000001d, 0x00000002, 0x00000010, 0x0003001d,
    0x00000020, 0x00000010, 0x0003001e, 0x00000021, 0x00000020, 0x00040020, 0x00000022, 0x00000002,
    0x00000021, 0x0004003b, 0x00000022, 0x00000023, 0x00000002, 0x0004002b, 0x00000006, 0x00000029,
    0x00000100, 0x0004002b, 0x00000006, 0x0000002a, 0x00000001, 0x0006002c, 0x00000009, 0x0000002b,
    0x00000029, 0x0000002a, 0x0000002a, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003b, 0x00000007, 0x00000008, 0x00000007, 0x00050041, 0x0000000d,
    0x0000000e, 0x0000000b, 0x0000000c, 0x0004003d, 0x00000006, 0x0000000f, 0x0000000e, 0x0003003e,
    0x00000008, 0x0000000f, 0x0004003d, 0x00000006, 0x00000017, 0x00000008, 0x0004003d, 0x00000006,
    0x0000001c, 0x00000008, 0x00060041, 0x0000001d, 0x0000001e, 0x0000001b, 0x00000016, 0x0000001c,
    0x0004003d, 0x00000010, 0x0000001f, 0x0000001e, 0x0004003d, 0x00000006, 0x00000024, 0x00000008,
    0x00060041, 0x0000001d, 0x00000025, 0x00000023, 0x00000016, 0x00000024, 0x0004003d, 0x00000010,
    0x00000026, 0x00000025, 0x00050081, 0x00000010, 0x00000027, 0x0000001f, 0x00000026, 0x00060041,
    0x0000001d, 0x00000028, 0x00000014, 0x00000016, 0x00000017, 0x0003003e, 0x00000028, 0x00000027,
    0x000100fd, 0x00010038,
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

bool create_instance(VkInstance* instance) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 9";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkResult result = vkCreateInstance(&create_info, nullptr, instance);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    TEST_LOG_INFO() << "✅ vkCreateInstance succeeded\n";
    return true;
}

bool pick_physical_device(VkInstance instance, VkPhysicalDevice* physical_device) {
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        TEST_LOG_ERROR() << "✗ Failed to enumerate physical devices\n";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS || devices.empty()) {
        TEST_LOG_ERROR() << "✗ vkEnumeratePhysicalDevices failed: " << result << "\n";
        return false;
    }
    *physical_device = devices[0];
    return true;
}

uint32_t select_queue_family(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }
    return 0;
}

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

bool create_buffer(VkDevice device,
                   VkPhysicalDevice physical_device,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties,
                   BufferResource* out) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, &out->buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer failed: " << result << "\n";
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(device, out->buffer, &requirements);

    VkPhysicalDeviceMemoryProperties mem_props = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    uint32_t type_index = find_memory_type(requirements.memoryTypeBits, properties, mem_props);
    if (type_index == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ Unable to find buffer memory type\n";
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = type_index;

    result = vkAllocateMemory(device, &alloc_info, nullptr, &out->memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory failed: " << result << "\n";
        return false;
    }

    vkBindBufferMemory(device, out->buffer, out->memory, 0);
    return true;
}

void destroy_buffer(VkDevice device, BufferResource* buffer) {
    if (buffer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer->buffer, nullptr);
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer->memory, nullptr);
    }
    buffer->buffer = VK_NULL_HANDLE;
    buffer->memory = VK_NULL_HANDLE;
}

bool flush_memory(VkDevice device,
                  VkDeviceMemory memory,
                  VkDeviceSize size) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = 0;
    range.size = size;
    return vkFlushMappedMemoryRanges(device, 1, &range) == VK_SUCCESS;
}

bool invalidate_memory(VkDevice device,
                       VkDeviceMemory memory,
                       VkDeviceSize size) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = 0;
    range.size = size;
    return vkInvalidateMappedMemoryRanges(device, 1, &range) == VK_SUCCESS;
}

} // namespace

bool run_phase09_test() {
    TEST_LOG_INFO() << "\n========================================\n";
    TEST_LOG_INFO() << "Phase 9: Compute Shader\n";
    TEST_LOG_INFO() << "========================================\n\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shader_module = VK_NULL_HANDLE;
    BufferResource buffer_a = {};
    BufferResource buffer_b = {};
    BufferResource buffer_out = {};
    float* data_a = nullptr;
    float* data_b = nullptr;
    float* data_out = nullptr;
    bool success = false;

    do {
        if (!create_instance(&instance)) {
            break;
        }
        if (!pick_physical_device(instance, &physical_device)) {
            break;
        }

        uint32_t queue_family = select_queue_family(physical_device);
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        VkDeviceCreateInfo device_info = {};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;

        VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
        if (result != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << result << "\n";
            break;
        }

        vkGetDeviceQueue(device, queue_family, 0, &queue);
        if (queue == VK_NULL_HANDLE) {
            TEST_LOG_ERROR() << "✗ vkGetDeviceQueue returned NULL\n";
            break;
        }

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue_family;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateCommandPool failed\n";
            break;
        }

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &alloc_info, &command_buffer) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkAllocateCommandBuffers failed\n";
            break;
        }

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateFence failed\n";
            break;
        }

        VkMemoryPropertyFlags host_visible =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!create_buffer(device,
                           physical_device,
                           kBufferSize,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           host_visible,
                           &buffer_a) ||
            !create_buffer(device,
                           physical_device,
                           kBufferSize,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           host_visible,
                           &buffer_b) ||
            !create_buffer(device,
                           physical_device,
                           kBufferSize,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           host_visible,
                           &buffer_out)) {
            break;
        }

        vkMapMemory(device, buffer_a.memory, 0, kBufferSize, 0, reinterpret_cast<void**>(&data_a));
        vkMapMemory(device, buffer_b.memory, 0, kBufferSize, 0, reinterpret_cast<void**>(&data_b));
        vkMapMemory(device, buffer_out.memory, 0, kBufferSize, 0, reinterpret_cast<void**>(&data_out));
        for (uint32_t i = 0; i < kElementCount; ++i) {
            data_a[i] = static_cast<float>(i + 1);
            data_b[i] = static_cast<float>((i + 1) * 10);
            data_out[i] = 0.0f;
        }
        flush_memory(device, buffer_a.memory, kBufferSize);
        flush_memory(device, buffer_b.memory, kBufferSize);
        flush_memory(device, buffer_out.memory, kBufferSize);

        VkDescriptorSetLayoutBinding bindings[3] = {};
        for (uint32_t i = 0; i < 3; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 3;
        layout_info.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout) !=
            VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateDescriptorSetLayout failed\n";
            break;
        }

        VkDescriptorPoolSize pool_sizes = {};
        pool_sizes.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes.descriptorCount = 3;

        VkDescriptorPoolCreateInfo descriptor_pool_info = {};
        descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_info.maxSets = 1;
        descriptor_pool_info.poolSizeCount = 1;
        descriptor_pool_info.pPoolSizes = &pool_sizes;
        if (vkCreateDescriptorPool(device, &descriptor_pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateDescriptorPool failed\n";
            break;
        }

        VkDescriptorSetAllocateInfo descriptor_alloc = {};
        descriptor_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_alloc.descriptorPool = descriptor_pool;
        descriptor_alloc.descriptorSetCount = 1;
        descriptor_alloc.pSetLayouts = &descriptor_set_layout;
        if (vkAllocateDescriptorSets(device, &descriptor_alloc, &descriptor_set) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkAllocateDescriptorSets failed\n";
            break;
        }

        VkDescriptorBufferInfo buffer_infos[3] = {};
        buffer_infos[0].buffer = buffer_a.buffer;
        buffer_infos[0].offset = 0;
        buffer_infos[0].range = kBufferSize;
        buffer_infos[1].buffer = buffer_b.buffer;
        buffer_infos[1].offset = 0;
        buffer_infos[1].range = kBufferSize;
        buffer_infos[2].buffer = buffer_out.buffer;
        buffer_infos[2].offset = 0;
        buffer_infos[2].range = kBufferSize;

        VkWriteDescriptorSet descriptor_writes[3] = {};
        for (uint32_t i = 0; i < 3; ++i) {
            descriptor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_writes[i].dstSet = descriptor_set;
            descriptor_writes[i].dstBinding = i;
            descriptor_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptor_writes[i].descriptorCount = 1;
            descriptor_writes[i].pBufferInfo = &buffer_infos[i];
        }
        vkUpdateDescriptorSets(device, 3, descriptor_writes, 0, nullptr);

        VkPipelineLayoutCreateInfo pipeline_layout_info = {};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
        if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout) !=
            VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreatePipelineLayout failed\n";
            break;
        }

        VkShaderModuleCreateInfo shader_info = {};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = sizeof(kSimpleAddSpirv);
        shader_info.pCode = kSimpleAddSpirv;
        if (vkCreateShaderModule(device, &shader_info, nullptr, &shader_module) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateShaderModule failed\n";
            break;
        }

        VkPipelineShaderStageCreateInfo stage_info = {};
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage_info.module = shader_module;
        stage_info.pName = "main";

        VkComputePipelineCreateInfo compute_info = {};
        compute_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        compute_info.stage = stage_info;
        compute_info.layout = pipeline_layout;

        if (vkCreateComputePipelines(device,
                                     VK_NULL_HANDLE,
                                     1,
                                     &compute_info,
                                     nullptr,
                                     &pipeline) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateComputePipelines failed\n";
            break;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &begin_info);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout,
                                0,
                                1,
                                &descriptor_set,
                                0,
                                nullptr);
        vkCmdDispatch(command_buffer, kElementCount / 256, 1, 1);
        vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;

        vkResetFences(device, 1, &fence);
        if (vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkQueueSubmit failed\n";
            break;
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        invalidate_memory(device, buffer_out.memory, kBufferSize);
        bool valid = true;
        for (uint32_t i = 0; i < kElementCount; ++i) {
            float expected = data_a[i] + data_b[i];
            if (std::abs(data_out[i] - expected) > 0.001f) {
                TEST_LOG_ERROR() << "✗ Mismatch at " << i << ": got " << data_out[i]
                          << " expected " << expected << "\n";
                valid = false;
                break;
            }
        }

        if (!valid) {
            break;
        }

        TEST_LOG_INFO() << "✅ Phase 9 compute shader executed successfully!\n";
        success = true;
    } while (false);

    if (data_a) {
        vkUnmapMemory(device, buffer_a.memory);
    }
    if (data_b) {
        vkUnmapMemory(device, buffer_b.memory);
    }
    if (data_out) {
        vkUnmapMemory(device, buffer_out.memory);
    }

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (shader_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, shader_module, nullptr);
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    }
    if (descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    }
    destroy_buffer(device, &buffer_out);
    destroy_buffer(device, &buffer_b);
    destroy_buffer(device, &buffer_a);

    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    return success;
}
