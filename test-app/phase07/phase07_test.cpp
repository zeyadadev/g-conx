#include "phase07_test.h"

#include <chrono>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

uint32_t find_memory_type(uint32_t type_bits,
                          VkMemoryPropertyFlags desired,
                          const VkPhysicalDeviceMemoryProperties& mem_props) {
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & desired) == desired) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool create_instance(VkInstance* instance) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 7 Test";
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
        std::cerr << "✗ vkEnumeratePhysicalDevices (2nd call) failed: " << result << "\n";
        return false;
    }
    *out_device = devices[0];
    return true;
}

uint32_t select_queue_family(VkPhysicalDevice physical_device) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return 0;
}

} // namespace

bool run_phase07_test() {
    std::cout << "\n========================================\n";
    std::cout << "Phase 7: Real GPU Execution\n";
    std::cout << "========================================\n\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkBuffer test_buffer = VK_NULL_HANDLE;
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props = {};
    bool success = false;

    do {
        if (!create_instance(&instance)) {
            break;
        }

        if (!pick_physical_device(instance, &physical_device)) {
            break;
        }

        vkGetPhysicalDeviceProperties(physical_device, nullptr);
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
        buffer_info.size = 4096;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        result = vkCreateBuffer(device, &buffer_info, nullptr, &test_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateBuffer failed: " << result << "\n";
            break;
        }
        std::cout << "✅ Buffer created\n";

        VkMemoryRequirements requirements = {};
        vkGetBufferMemoryRequirements(device, test_buffer, &requirements);

        uint32_t memory_type =
            find_memory_type(requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             mem_props);
        if (memory_type == UINT32_MAX) {
            std::cerr << "✗ Failed to find DEVICE_LOCAL memory type\n";
            break;
        }

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = memory_type;

        result = vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkAllocateMemory failed: " << result << "\n";
            break;
        }
        std::cout << "✅ Device memory allocated\n";

        result = vkBindBufferMemory(device, test_buffer, buffer_memory, 0);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkBindBufferMemory failed: " << result << "\n";
            break;
        }
        std::cout << "✅ Buffer memory bound\n";

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue_family_index;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateCommandPool failed: " << result << "\n";
            break;
        }

        VkCommandBufferAllocateInfo alloc_cb = {};
        alloc_cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_cb.commandPool = command_pool;
        alloc_cb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_cb.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &alloc_cb, &command_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkAllocateCommandBuffers failed: " << result << "\n";
            break;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkBeginCommandBuffer failed: " << result << "\n";
            break;
        }

        vkCmdFillBuffer(command_buffer, test_buffer, 0, buffer_info.size, 0x5a5a5a5au);

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkEndCommandBuffer failed: " << result << "\n";
            break;
        }

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = vkCreateFence(device, &fence_info, nullptr, &fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateFence failed: " << result << "\n";
            break;
        }

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;

        auto start = std::chrono::high_resolution_clock::now();
        result = vkQueueSubmit(queue, 1, &submit, fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkQueueSubmit failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkQueueSubmit issued real GPU work\n";

        result = vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkWaitForFences timed out or failed: " << result << "\n";
            break;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "✅ Fence signaled after GPU execution (" << duration_ms << " ms)\n";

        result = vkGetFenceStatus(device, fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkGetFenceStatus did not report success: " << result << "\n";
            break;
        }
        std::cout << "✅ vkGetFenceStatus reports success\n";

        success = true;
    } while (false);

    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
    }
    if (command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    if (test_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, test_buffer, nullptr);
    }
    if (buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer_memory, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    if (success) {
        std::cout << "✅ Phase 7 PASSED\n";
    }
    return success;
}
