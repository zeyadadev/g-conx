#include "phase06_test.h"

#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

bool create_instance(VkInstance* instance) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 6 Test";
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
    if (count == 0) {
        return 0;
    }
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

bool run_phase06_test() {
    std::cout << "\n========================================\n";
    std::cout << "Phase 6: Fake Command Submission\n";
    std::cout << "========================================\n\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore wait_semaphore = VK_NULL_HANDLE;
    VkSemaphore signal_semaphore = VK_NULL_HANDLE;
    const uint64_t timeout_ns = 1'000'000'000ull; // 1 second
    VkSemaphore wait_semaphores[1] = {};
    VkSemaphore signal_semaphores[1] = {};
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo signal_submit = {};
    VkSubmitInfo submit_info = {};
    bool success = false;

    do {
        if (!create_instance(&instance)) {
            break;
        }

        if (!pick_physical_device(instance, &physical_device)) {
            break;
        }

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

        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue_family_index;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateCommandPool failed: " << result << "\n";
            break;
        }

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
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
        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkEndCommandBuffer failed: " << result << "\n";
            break;
        }

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = 0;
        result = vkCreateFence(device, &fence_info, nullptr, &fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateFence failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkCreateFence succeeded\n";

        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(device, &semaphore_info, nullptr, &wait_semaphore);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateSemaphore (wait) failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkCreateSemaphore (wait) succeeded\n";

        result = vkCreateSemaphore(device, &semaphore_info, nullptr, &signal_semaphore);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkCreateSemaphore (signal) failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkCreateSemaphore (signal) succeeded\n";

        wait_semaphores[0] = wait_semaphore;
        signal_semaphores[0] = signal_semaphore;

        // Pre-signal the wait semaphore with an empty submission
        signal_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        signal_submit.signalSemaphoreCount = 1;
        signal_submit.pSignalSemaphores = wait_semaphores;
        result = vkQueueSubmit(queue, 1, &signal_submit, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ Initial vkQueueSubmit failed: " << result << "\n";
            break;
        }
        vkQueueWaitIdle(queue);

        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        result = vkQueueSubmit(queue, 1, &submit_info, fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkQueueSubmit failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkQueueSubmit succeeded\n";

        result = vkWaitForFences(device, 1, &fence, VK_TRUE, timeout_ns);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkWaitForFences failed: " << result << "\n";
            break;
        }
        std::cout << "✅ vkWaitForFences (timeout=1s) succeeded\n";

        result = vkGetFenceStatus(device, fence);
        if (result != VK_SUCCESS) {
            std::cerr << "✗ vkGetFenceStatus did not report success: " << result << "\n";
            break;
        }
        std::cout << "✅ Fence signaled immediately (fake execution)\n";

        vkQueueWaitIdle(queue);
        std::cout << "✅ vkQueueWaitIdle succeeded\n";

        success = true;
    } while (false);

    if (signal_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, signal_semaphore, nullptr);
    }
    if (wait_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, wait_semaphore, nullptr);
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
    }
    if (command_buffer != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    if (success) {
        std::cout << "✅ Phase 6 PASSED\n";
    }
    return success;
}
