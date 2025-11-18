#include "phase03_test.h"
#include <cstring>
#include <iostream>
#include <vector>

static void print_properties(const VkPhysicalDeviceProperties& props) {
    std::cout << "  Device Name: " << props.deviceName << "\n";
    std::cout << "  Device Type: ";
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            std::cout << "Discrete GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            std::cout << "Integrated GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            std::cout << "Virtual GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            std::cout << "CPU";
            break;
        default:
            std::cout << "Other";
            break;
    }
    std::cout << "\n";

    uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
    uint32_t patch = VK_VERSION_PATCH(props.apiVersion);
    std::cout << "  API Version: " << major << "." << minor << "." << patch << "\n";
    std::cout << "  Driver Version: " << VK_VERSION_MAJOR(props.driverVersion) << "."
              << VK_VERSION_MINOR(props.driverVersion) << "."
              << VK_VERSION_PATCH(props.driverVersion) << "\n";
    std::cout << "  Vendor ID: 0x" << std::hex << props.vendorID << std::dec << "\n";
    std::cout << "  Device ID: 0x" << std::hex << props.deviceID << std::dec << "\n";
}

static uint32_t count_enabled_features(const VkPhysicalDeviceFeatures& features) {
    uint32_t count = 0;
    const VkBool32* feature_array = reinterpret_cast<const VkBool32*>(&features);
    size_t num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);

    for (size_t i = 0; i < num_features; i++) {
        if (feature_array[i] == VK_TRUE) {
            count++;
        }
    }
    return count;
}

static void print_queue_family(uint32_t index, const VkQueueFamilyProperties& props) {
    std::cout << "  Family " << index << ": ";

    if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        std::cout << "Graphics | ";
    }
    if (props.queueFlags & VK_QUEUE_COMPUTE_BIT) {
        std::cout << "Compute | ";
    }
    if (props.queueFlags & VK_QUEUE_TRANSFER_BIT) {
        std::cout << "Transfer | ";
    }
    if (props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) {
        std::cout << "Sparse | ";
    }

    std::cout << props.queueCount << " queues\n";
}

bool run_phase03_test() {
    std::cout << "\n========================================\n";
    std::cout << "Phase 3: Fake Device Creation\n";
    std::cout << "========================================\n\n";

    VkResult result;

    // Step 1: Create instance
    std::cout << "[1] Creating instance...\n";
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phase 3 Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    std::cout << "✓ Instance created\n\n";

    // Step 2: Enumerate physical devices
    std::cout << "[2] Enumerating physical devices...\n";
    uint32_t deviceCount = 0;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS || deviceCount == 0) {
        std::cerr << "✗ vkEnumeratePhysicalDevices failed or no devices\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkEnumeratePhysicalDevices failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Found " << deviceCount << " physical device(s)\n\n";

    VkPhysicalDevice physicalDevice = physicalDevices[0];

    // Step 3: Get physical device properties
    std::cout << "[3] Querying physical device properties...\n";
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    print_properties(properties);

    // Verify the device name
    if (strcmp(properties.deviceName, "Venus Plus Virtual GPU") != 0) {
        std::cerr << "✗ Unexpected device name: " << properties.deviceName << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Physical device properties retrieved\n\n";

    // Step 4: Get physical device features
    std::cout << "[4] Querying physical device features...\n";
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    uint32_t enabledFeatureCount = count_enabled_features(features);
    std::cout << "  Enabled features: " << enabledFeatureCount << "\n";

    if (enabledFeatureCount == 0) {
        std::cerr << "✗ No features enabled (expected some features)\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Physical device features retrieved\n\n";

    // Step 5: Get queue family properties
    std::cout << "[5] Querying queue family properties...\n";
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    if (queueFamilyCount == 0) {
        std::cerr << "✗ No queue families found\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::cout << "  Queue families: " << queueFamilyCount << "\n";
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        print_queue_family(i, queueFamilies[i]);
    }

    // Verify queue family 0 has graphics + compute + transfer
    if (!(queueFamilies[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
        !(queueFamilies[0].queueFlags & VK_QUEUE_COMPUTE_BIT) ||
        !(queueFamilies[0].queueFlags & VK_QUEUE_TRANSFER_BIT)) {
        std::cerr << "✗ Queue family 0 missing expected flags\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Queue family properties retrieved\n\n";

    // Step 6: Get memory properties
    std::cout << "[6] Querying memory properties...\n";
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    std::cout << "  Memory types: " << memProperties.memoryTypeCount << "\n";
    std::cout << "  Memory heaps: " << memProperties.memoryHeapCount << "\n";

    if (memProperties.memoryTypeCount < 2 || memProperties.memoryHeapCount < 2) {
        std::cerr << "✗ Expected at least 2 memory types and 2 heaps\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Memory properties retrieved\n\n";

    // Step 7: Create device
    std::cout << "[7] Creating logical device...\n";

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 0;
    deviceCreateInfo.ppEnabledExtensionNames = nullptr;
    deviceCreateInfo.pEnabledFeatures = nullptr;

    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateDevice failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Logical device created\n\n";

    // Step 8: Get device queue
    std::cout << "[8] Getting device queue...\n";
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    if (queue == VK_NULL_HANDLE) {
        std::cerr << "✗ vkGetDeviceQueue returned NULL\n";
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✓ Device queue retrieved (handle: " << queue << ")\n\n";

    // Step 9: Destroy device
    std::cout << "[9] Destroying device...\n";
    vkDestroyDevice(device, nullptr);
    std::cout << "✓ Device destroyed\n\n";

    // Step 10: Destroy instance
    std::cout << "[10] Destroying instance...\n";
    vkDestroyInstance(instance, nullptr);
    std::cout << "✓ Instance destroyed\n\n";

    std::cout << "========================================\n";
    std::cout << "✓ Phase 3 PASSED\n";
    std::cout << "========================================\n\n";

    return true;
}
