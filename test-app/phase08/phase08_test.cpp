#include "phase08_test.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

constexpr VkDeviceSize kBufferSize = 1024ull * 1024ull;
constexpr uint32_t kPattern = 0x12345678u;

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
    app_info.pApplicationName = "Phase 8 Test";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkResult result = vkCreateInstance(&create_info, nullptr, instance);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    std::cout << "✅ vkCreateInstance succeeded\n";
    return true;
}

bool pick_physical_device(VkInstance instance, VkPhysicalDevice* out_device) {
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        std::cerr << "✗ Failed to enumerate physical devices\n";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS || devices.empty()) {
        std::cerr << "✗ vkEnumeratePhysicalDevices failed: " << result << "\n";
        return false;
    }
    *out_device = devices[0];
    return true;
}

uint32_t select_queue_family(VkPhysicalDevice physical_device) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return 0;
}

} // namespace

bool run_phase08_test() {
    std::cout << "\n========================================\n";
    std::cout << "Phase 8: Memory Data Transfer\n";
    std::cout << "========================================\n\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props = {};
    bool success = false;

    do {
        if (!create_instance(&instance)) {
            break;
        }

        if (!pick_physical_device(instance, &physical_device)) {
            break;
        }

        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

        uint32_t queue_family_index = select_queue_family(physical_device);
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family_index;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        VkDeviceCreateInfo device_info = {};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;

        VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateDevice failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkCreateDevice succeeded\n";

        vkGetDeviceQueue(device, queue_family_index, 0, &queue);
        if (queue == VK_NULL_HANDLE) {
            std::cerr << "✗ vkGetDeviceQueue returned NULL\n";
            break;
        }

        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = kBufferSize;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        result = vkCreateBuffer(device, &buffer_info, nullptr, &staging_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateBuffer (staging) failed: " << result << "\n";
            break;
        }

        result = vkCreateBuffer(device, &buffer_info, nullptr, &device_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateBuffer (device) failed: " << result << "\n";
            break;
        }

        VkMemoryRequirements staging_reqs = {};
        vkGetBufferMemoryRequirements(device, staging_buffer, &staging_reqs);
        VkMemoryRequirements device_reqs = {};
        vkGetBufferMemoryRequirements(device, device_buffer, &device_reqs);

        uint32_t staging_type = find_memory_type(staging_reqs.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                 mem_props);
        if (staging_type == UINT32_MAX) {
            std::cerr << "✗ Failed to find HOST_VISIBLE|HOST_COHERENT memory type\n";
            break;
        }

        uint32_t device_type = find_memory_type(device_reqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                mem_props);
        if (device_type == UINT32_MAX) {
            // Fallback: reuse staging type
            device_type = staging_type;
        }

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = staging_reqs.size;
        alloc_info.memoryTypeIndex = staging_type;
        result = vkAllocateMemory(device, &alloc_info, nullptr, &staging_memory);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkAllocateMemory (staging) failed: " << result << "\n";
            break;
        }

        alloc_info.allocationSize = device_reqs.size;
        alloc_info.memoryTypeIndex = device_type;
        result = vkAllocateMemory(device, &alloc_info, nullptr, &device_memory);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkAllocateMemory (device) failed: " << result << "\n";
            break;
        }

        vkBindBufferMemory(device, staging_buffer, staging_memory, 0);
        vkBindBufferMemory(device, device_buffer, device_memory, 0);

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue_family_index;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateCommandPool failed: " << result << "\n";
            break;
        }

        VkCommandBufferAllocateInfo cb_alloc = {};
        cb_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cb_alloc.commandPool = command_pool;
        cb_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cb_alloc.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &cb_alloc, &command_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkAllocateCommandBuffers failed: " << result << "\n";
            break;
        }

        void* mapped = nullptr;
        result = vkMapMemory(device, staging_memory, 0, kBufferSize, 0, &mapped);
        if (result != VK_SUCCESS || !mapped) {
            std::cerr << "✗ vkMapMemory failed: " << result << "\n";
            break;
        }
        std::cout << "✅ Mapped staging buffer memory\n";

        uint32_t* ptr = reinterpret_cast<uint32_t*>(mapped);
        const uint32_t element_count = static_cast<uint32_t>(kBufferSize / sizeof(uint32_t));
        for (uint32_t i = 0; i < element_count; ++i) {
            ptr[i] = kPattern;
        }
        std::cout << "✅ Wrote test pattern (" << element_count << " uint32_t values)\n";

        std::cout << "✅ Unmapping... transferring " << kBufferSize << " bytes to server\n";
        auto transfer_start = std::chrono::high_resolution_clock::now();
        vkUnmapMemory(device, staging_memory);
        auto transfer_end = std::chrono::high_resolution_clock::now();
        double transfer_ms =
            std::chrono::duration<double, std::milli>(transfer_end - transfer_start).count();
        double throughput = (static_cast<double>(kBufferSize) / (1024.0 * 1024.0)) /
                            (std::max(transfer_ms, 0.0001) / 1000.0);
        std::cout << "✅ Transfer complete (took " << transfer_ms << " ms, "
                  << throughput << " MB/s)\n";

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkBeginCommandBuffer failed: " << result << "\n";
            break;
        }

        VkBufferCopy copy = {};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = kBufferSize;
        vkCmdCopyBuffer(command_buffer, staging_buffer, device_buffer, 1, &copy);
        vkCmdFillBuffer(command_buffer, staging_buffer, 0, kBufferSize, 0);
        vkCmdCopyBuffer(command_buffer, device_buffer, staging_buffer, 1, &copy);

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkEndCommandBuffer failed: " << result << "\n";
            break;
        }
        std::cout << "✅ Recorded buffer copy commands\n";

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkQueueSubmit failed: " << result << "\n";
            break;
        }

        result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkQueueWaitIdle failed: " << result << "\n";
            break;
        }
        std::cout << "✅ GPU copy complete\n";

        void* read_ptr = nullptr;
        auto read_start = std::chrono::high_resolution_clock::now();
        result = vkMapMemory(device, staging_memory, 0, kBufferSize, 0, &read_ptr);
        auto read_end = std::chrono::high_resolution_clock::now();
        if (result != VK_SUCCESS || !read_ptr) {
            std::cerr << "✗ vkMapMemory (readback) failed: " << result << "\n";
            break;
        }
        double read_ms =
            std::chrono::duration<double, std::milli>(read_end - read_start).count();
        std::cout << "✅ Read " << kBufferSize << " bytes from server (" << read_ms << " ms)\n";

        const uint32_t* read_data = reinterpret_cast<const uint32_t*>(read_ptr);
        bool valid = true;
        for (uint32_t i = 0; i < element_count; ++i) {
            if (read_data[i] != kPattern) {
                std::cerr << "✗ Data mismatch at index " << i << ": expected "
                          << std::hex << kPattern << ", got " << read_data[i] << std::dec << "\n";
                valid = false;
                break;
            }
        }

        vkUnmapMemory(device, staging_memory);

        if (!valid) {
            break;
        }

        std::cout << "✅ Data verification: PASSED\n";
        success = true;
    } while (false);

    if (device != VK_NULL_HANDLE) {
        if (command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        }
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
        }
        if (device_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, device_buffer, nullptr);
        }
        if (staging_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, staging_buffer, nullptr);
        }
        if (device_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, device_memory, nullptr);
        }
        if (staging_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, staging_memory, nullptr);
        }
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    if (success) {
        std::cout << "✅ Phase 8 PASSED\n";
        return true;
    }

    std::cerr << "✗ Phase 8 FAILED\n";
    return false;
}
