#include "phase02_test.h"
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>

namespace phase02 {

int run_test() {
    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "Phase 2: Fake Instance Creation\n";
    std::cout << "=================================================\n\n";

    VkResult result;

    // Step 1: Create Instance
    std::cout << "Step 1: Creating Vulkan instance...\n";

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Venus Plus Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = 0;
    createInfo.enabledExtensionCount = 0;

    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&createInfo, nullptr, &instance);

    if (result != VK_SUCCESS) {
        std::cerr << "  FAILED: vkCreateInstance returned " << result << "\n";
        return 1;
    }

    if (instance == VK_NULL_HANDLE) {
        std::cerr << "  FAILED: Instance handle is NULL\n";
        return 1;
    }

    std::cout << "  SUCCESS: Instance created\n";
    std::cout << "  Instance handle: " << instance << "\n\n";

    // Step 2: Enumerate Physical Devices (first call - get count)
    std::cout << "Step 2: Enumerating physical devices (get count)...\n";

    uint32_t deviceCount = 0;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "  FAILED: vkEnumeratePhysicalDevices (count) returned " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::cout << "  SUCCESS: Found " << deviceCount << " physical device(s)\n\n";

    if (deviceCount == 0) {
        std::cerr << "  FAILED: No physical devices found\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    // Step 3: Enumerate Physical Devices (second call - get devices)
    std::cout << "Step 3: Enumerating physical devices (get devices)...\n";

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

    if (result != VK_SUCCESS) {
        std::cerr << "  FAILED: vkEnumeratePhysicalDevices (devices) returned " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::cout << "  SUCCESS: Retrieved physical devices\n";
    for (uint32_t i = 0; i < deviceCount; i++) {
        std::cout << "  Physical device " << i << ": " << physicalDevices[i] << "\n";
        if (physicalDevices[i] == VK_NULL_HANDLE) {
            std::cerr << "  FAILED: Physical device " << i << " is NULL\n";
            vkDestroyInstance(instance, nullptr);
            return 1;
        }
    }
    std::cout << "\n";

    // Step 4: Destroy Instance
    std::cout << "Step 4: Destroying instance...\n";
    vkDestroyInstance(instance, nullptr);
    std::cout << "  SUCCESS: Instance destroyed\n\n";

    // Summary
    std::cout << "=================================================\n";
    std::cout << "Phase 2 PASSED\n";
    std::cout << "=================================================\n\n";

    return 0;
}

} // namespace phase02
