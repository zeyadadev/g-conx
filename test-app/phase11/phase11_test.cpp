#include "phase11_test.h"

#include "logging.h"
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr VkDeviceSize kSparseBufferSize = 4096;
constexpr uint32_t kPattern = 0x7b7ba1u;

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
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
    app_info.pApplicationName = "Phase 11 Sparse Binding";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkResult result = vkCreateInstance(&create_info, nullptr, instance);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed: " << result;
        return false;
    }
    return true;
}

bool pick_physical_device(VkInstance instance, VkPhysicalDevice* out_device) {
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        TEST_LOG_ERROR() << "✗ Failed to enumerate physical devices";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS || devices.empty()) {
        TEST_LOG_ERROR() << "✗ vkEnumeratePhysicalDevices failed: " << result;
        return false;
    }
    *out_device = devices[0];
    return true;
}

uint32_t find_sparse_queue_family(VkPhysicalDevice physical_device) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if ((props[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) &&
            (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool create_device(VkPhysicalDevice physical_device,
                   uint32_t queue_family_index,
                   VkDevice* out_device,
                   VkQueue* out_queue) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(physical_device, &features);
    if (!features.sparseBinding) {
        TEST_LOG_WARN() << "⚠️ Device does not support sparse binding, skipping phase";
        return false;
    }

    VkPhysicalDeviceFeatures enabled = {};
    enabled.sparseBinding = VK_TRUE;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &enabled;

    VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, out_device);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << result;
        return false;
    }

    vkGetDeviceQueue(*out_device, queue_family_index, 0, out_queue);
    if (*out_queue == VK_NULL_HANDLE) {
        TEST_LOG_ERROR() << "✗ vkGetDeviceQueue returned NULL";
        return false;
    }
    return true;
}

bool create_host_buffer(VkDevice device,
                        const VkPhysicalDeviceMemoryProperties& mem_props,
                        VkDeviceSize size,
                        BufferResource* out) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, &out->buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer failed: " << result;
        return false;
    }

    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, out->buffer, &reqs);

    uint32_t memory_type = find_memory_type(reqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            mem_props);
    if (memory_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ No HOST_VISIBLE memory type found";
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = memory_type;

    result = vkAllocateMemory(device, &alloc_info, nullptr, &out->memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory failed: " << result;
        return false;
    }

    result = vkBindBufferMemory(device, out->buffer, out->memory, 0);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkBindBufferMemory failed: " << result;
        return false;
    }
    return true;
}

bool write_pattern(VkDevice device, const BufferResource& buffer, VkDeviceSize size) {
    void* mapped = nullptr;
    VkResult result = vkMapMemory(device, buffer.memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkMapMemory (write) failed: " << result;
        return false;
    }
    uint32_t* data = static_cast<uint32_t*>(mapped);
    const size_t words = static_cast<size_t>(size / sizeof(uint32_t));
    for (size_t i = 0; i < words; ++i) {
        data[i] = kPattern + static_cast<uint32_t>(i);
    }
    vkUnmapMemory(device, buffer.memory);
    return true;
}

bool clear_buffer(VkDevice device, const BufferResource& buffer, VkDeviceSize size) {
    void* mapped = nullptr;
    VkResult result = vkMapMemory(device, buffer.memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkMapMemory (clear) failed: " << result;
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(size));
    vkUnmapMemory(device, buffer.memory);
    return true;
}

bool verify_pattern(VkDevice device, const BufferResource& buffer, VkDeviceSize size) {
    void* mapped = nullptr;
    VkResult result = vkMapMemory(device, buffer.memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkMapMemory (verify) failed: " << result;
        return false;
    }
    const uint32_t* data = static_cast<const uint32_t*>(mapped);
    const size_t words = static_cast<size_t>(size / sizeof(uint32_t));
    bool ok = true;
    for (size_t i = 0; i < words; ++i) {
        uint32_t expected = kPattern + static_cast<uint32_t>(i);
        if (data[i] != expected) {
            TEST_LOG_ERROR() << "✗ Buffer mismatch at word " << i << ": got " << data[i]
                             << " expected " << expected;
            ok = false;
            break;
        }
    }
    vkUnmapMemory(device, buffer.memory);
    return ok;
}

bool create_sparse_buffer(VkDevice device, VkDeviceSize size, VkBuffer* out_buffer) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT;

    VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, out_buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer (sparse) failed: " << result;
        return false;
    }
    return true;
}

bool allocate_sparse_memory(VkDevice device,
                            const VkPhysicalDeviceMemoryProperties& mem_props,
                            const VkMemoryRequirements& reqs,
                            VkDeviceSize bind_size,
                            VkDeviceMemory* out_memory) {
    uint32_t memory_type = find_memory_type(reqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            mem_props);
    if (memory_type == UINT32_MAX) {
        memory_type = find_memory_type(reqs.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       mem_props);
    }
    if (memory_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ No compatible memory type for sparse buffer";
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = bind_size;
    alloc_info.memoryTypeIndex = memory_type;

    VkResult result = vkAllocateMemory(device, &alloc_info, nullptr, out_memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory (sparse) failed: " << result;
        return false;
    }
    return true;
}

bool bind_sparse_buffer(VkQueue queue,
                        VkBuffer buffer,
                        VkDeviceMemory memory,
                        VkDeviceSize size,
                        VkFence fence) {
    VkSparseMemoryBind memory_bind = {};
    memory_bind.resourceOffset = 0;
    memory_bind.size = size;
    memory_bind.memory = memory;
    memory_bind.memoryOffset = 0;
    memory_bind.flags = 0;

    VkSparseBufferMemoryBindInfo buffer_bind = {};
    buffer_bind.buffer = buffer;
    buffer_bind.bindCount = 1;
    buffer_bind.pBinds = &memory_bind;

    VkBindSparseInfo bind_info = {};
    bind_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
    bind_info.bufferBindCount = 1;
    bind_info.pBufferBinds = &buffer_bind;

    VkResult result = vkQueueBindSparse(queue, 1, &bind_info, fence);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkQueueBindSparse failed: " << result;
        return false;
    }
    return true;
}

bool record_and_submit_copy(VkDevice device,
                            VkQueue queue,
                            uint32_t queue_family_index,
                            VkBuffer src,
                            VkBuffer sparse,
                            VkBuffer dst,
                            VkFence fence,
                            VkDeviceSize size,
                            VkCommandPool* out_pool) {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family_index;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkResult result = vkCreateCommandPool(device, &pool_info, nullptr, out_pool);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateCommandPool failed: " << result;
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = *out_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers(device, &alloc_info, &cmd);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateCommandBuffers failed: " << result;
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkBeginCommandBuffer failed: " << result;
        return false;
    }

    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;

    vkCmdCopyBuffer(cmd, src, sparse, 1, &copy_region);

    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = sparse;
    barrier.offset = 0;
    barrier.size = size;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &barrier,
                         0,
                         nullptr);

    vkCmdCopyBuffer(cmd, sparse, dst, 1, &copy_region);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkEndCommandBuffer failed: " << result;
        return false;
    }

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    result = vkQueueSubmit(queue, 1, &submit_info, fence);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkQueueSubmit failed: " << result;
        return false;
    }
    return true;
}

} // namespace

bool run_phase11_test() {
    TEST_LOG_INFO() << "\n========================================";
    TEST_LOG_INFO() << "Phase 11: Sparse Binding Queue Exercise";
    TEST_LOG_INFO() << "========================================\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    BufferResource src = {};
    BufferResource dst = {};
    VkBuffer sparse_buffer = VK_NULL_HANDLE;
    VkDeviceMemory sparse_memory = VK_NULL_HANDLE;
    VkFence bind_fence = VK_NULL_HANDLE;
    VkFence submit_fence = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props = {};
    uint32_t queue_family_index = UINT32_MAX;
    VkDeviceSize buffer_size = kSparseBufferSize;
    bool success = false;

    do {
        if (!create_instance(&instance)) {
            break;
        }

        if (!pick_physical_device(instance, &physical_device)) {
            break;
        }

        VkPhysicalDeviceFeatures features = {};
        vkGetPhysicalDeviceFeatures(physical_device, &features);
        if (!features.sparseBinding) {
            TEST_LOG_WARN() << "⚠️ sparseBinding not supported on this device, skipping";
            success = true;
            break;
        }

        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

        queue_family_index = find_sparse_queue_family(physical_device);
        if (queue_family_index == UINT32_MAX) {
            TEST_LOG_WARN() << "⚠️ No queue family supports sparse binding, skipping";
            success = true;
            break;
        }

        if (!create_device(physical_device, queue_family_index, &device, &queue)) {
            break;
        }

        VkBuffer temp_sparse = VK_NULL_HANDLE;
        if (!create_sparse_buffer(device, buffer_size, &temp_sparse)) {
            break;
        }

        VkMemoryRequirements sparse_reqs_info = {};
        vkGetBufferMemoryRequirements(device, temp_sparse, &sparse_reqs_info);
        buffer_size = sparse_reqs_info.size;
        if (sparse_reqs_info.alignment > 0) {
            VkDeviceSize alignment = sparse_reqs_info.alignment;
            buffer_size = ((buffer_size + alignment - 1) / alignment) * alignment;
        }
        vkDestroyBuffer(device, temp_sparse, nullptr);

        if (!create_sparse_buffer(device, buffer_size, &sparse_buffer)) {
            break;
        }

        VkMemoryRequirements final_reqs = {};
        vkGetBufferMemoryRequirements(device, sparse_buffer, &final_reqs);

        if (!allocate_sparse_memory(device, mem_props, final_reqs, buffer_size, &sparse_memory)) {
            break;
        }

        if (!create_host_buffer(device, mem_props, buffer_size, &src)) {
            break;
        }
        if (!create_host_buffer(device, mem_props, buffer_size, &dst)) {
            break;
        }

        if (!write_pattern(device, src, buffer_size) || !clear_buffer(device, dst, buffer_size)) {
            break;
        }


        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device, &fence_info, nullptr, &bind_fence) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ Failed to create bind fence";
            break;
        }

        if (!bind_sparse_buffer(queue, sparse_buffer, sparse_memory, buffer_size, bind_fence)) {
            break;
        }

        VkResult wait_result = vkWaitForFences(device, 1, &bind_fence, VK_TRUE, UINT64_MAX);
        if (wait_result != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkWaitForFences (bind) failed: " << wait_result;
            break;
        }

        if (vkCreateFence(device, &fence_info, nullptr, &submit_fence) != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ Failed to create submit fence";
            break;
        }

        if (!record_and_submit_copy(device,
                                    queue,
                                    queue_family_index,
                                    src.buffer,
                                    sparse_buffer,
                                    dst.buffer,
                                    submit_fence,
                                    buffer_size,
                                    &command_pool)) {
            break;
        }

        wait_result = vkWaitForFences(device, 1, &submit_fence, VK_TRUE, UINT64_MAX);
        if (wait_result != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkWaitForFences (submit) failed: " << wait_result;
            break;
        }

        if (!verify_pattern(device, dst, buffer_size)) {
            break;
        }

        TEST_LOG_INFO() << "✅ vkQueueBindSparse copied data successfully";
        success = true;
    } while (false);

    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    if (submit_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, submit_fence, nullptr);
    }
    if (bind_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, bind_fence, nullptr);
    }
    if (sparse_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, sparse_buffer, nullptr);
    }
    if (sparse_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, sparse_memory, nullptr);
    }
    if (src.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, src.buffer, nullptr);
    }
    if (src.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, src.memory, nullptr);
    }
    if (dst.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, dst.buffer, nullptr);
    }
    if (dst.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, dst.memory, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    return success;
}
