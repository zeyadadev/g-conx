#include "phase03_test.h"
#include "logging.h"
#include <cstring>
#include <vector>
#include <iomanip>

static void print_properties(const VkPhysicalDeviceProperties& props) {
    TEST_LOG_INFO() << "  Device Name: " << props.deviceName << "\n";
    TEST_LOG_INFO() << "  Device Type: ";
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            TEST_LOG_INFO() << "Discrete GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            TEST_LOG_INFO() << "Integrated GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            TEST_LOG_INFO() << "Virtual GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            TEST_LOG_INFO() << "CPU";
            break;
        default:
            TEST_LOG_INFO() << "Other";
            break;
    }
    TEST_LOG_INFO() << "\n";

    uint32_t major = VK_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_VERSION_MINOR(props.apiVersion);
    uint32_t patch = VK_VERSION_PATCH(props.apiVersion);
    TEST_LOG_INFO() << "  API Version: " << major << "." << minor << "." << patch << "\n";
    TEST_LOG_INFO() << "  Driver Version: " << VK_VERSION_MAJOR(props.driverVersion) << "."
              << VK_VERSION_MINOR(props.driverVersion) << "."
              << VK_VERSION_PATCH(props.driverVersion) << "\n";
    TEST_LOG_INFO() << "  Vendor ID: 0x" << std::hex << props.vendorID << std::dec << "\n";
    TEST_LOG_INFO() << "  Device ID: 0x" << std::hex << props.deviceID << std::dec << "\n";
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
    TEST_LOG_INFO() << "  Family " << index << ": ";

    if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        TEST_LOG_INFO() << "Graphics | ";
    }
    if (props.queueFlags & VK_QUEUE_COMPUTE_BIT) {
        TEST_LOG_INFO() << "Compute | ";
    }
    if (props.queueFlags & VK_QUEUE_TRANSFER_BIT) {
        TEST_LOG_INFO() << "Transfer | ";
    }
    if (props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) {
        TEST_LOG_INFO() << "Sparse | ";
    }

    TEST_LOG_INFO() << props.queueCount << " queues\n";
}

bool run_phase03_test() {
    TEST_LOG_INFO() << "\n========================================\n";
    TEST_LOG_INFO() << "Phase 3: Fake Device Creation\n";
    TEST_LOG_INFO() << "========================================\n\n";

    VkResult result;

    // Step 1: Create instance
    TEST_LOG_INFO() << "[1] Creating instance...\n";
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
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    TEST_LOG_INFO() << "✓ Instance created\n\n";

    // Step 2: Enumerate physical devices
    TEST_LOG_INFO() << "[2] Enumerating physical devices...\n";
    uint32_t deviceCount = 0;
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS || deviceCount == 0) {
        TEST_LOG_ERROR() << "✗ vkEnumeratePhysicalDevices failed or no devices\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    result = vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkEnumeratePhysicalDevices failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Found " << deviceCount << " physical device(s)\n\n";

    VkPhysicalDevice physicalDevice = physicalDevices[0];

    // Step 3: Get physical device properties
    TEST_LOG_INFO() << "[3] Querying physical device properties...\n";
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    print_properties(properties);

    // Verify the device name
    if (strcmp(properties.deviceName, "Venus Plus Virtual GPU") != 0) {
        TEST_LOG_ERROR() << "✗ Unexpected device name: " << properties.deviceName << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Physical device properties retrieved\n\n";

    // Step 4: Get physical device features
    TEST_LOG_INFO() << "[4] Querying physical device features...\n";
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    uint32_t enabledFeatureCount = count_enabled_features(features);
    TEST_LOG_INFO() << "  Enabled features: " << enabledFeatureCount << "\n";

    if (enabledFeatureCount == 0) {
        TEST_LOG_ERROR() << "✗ No features enabled (expected some features)\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Physical device features retrieved\n\n";

    // Step 5: Get queue family properties
    TEST_LOG_INFO() << "[5] Querying queue family properties...\n";
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    if (queueFamilyCount == 0) {
        TEST_LOG_ERROR() << "✗ No queue families found\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    TEST_LOG_INFO() << "  Queue families: " << queueFamilyCount << "\n";
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        print_queue_family(i, queueFamilies[i]);
    }

    // Verify queue family 0 has graphics + compute + transfer
    if (!(queueFamilies[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
        !(queueFamilies[0].queueFlags & VK_QUEUE_COMPUTE_BIT) ||
        !(queueFamilies[0].queueFlags & VK_QUEUE_TRANSFER_BIT)) {
        TEST_LOG_ERROR() << "✗ Queue family 0 missing expected flags\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Queue family properties retrieved\n\n";

    // Step 6: Get memory properties
    TEST_LOG_INFO() << "[6] Querying memory properties...\n";
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    TEST_LOG_INFO() << "  Memory types: " << memProperties.memoryTypeCount << "\n";
    TEST_LOG_INFO() << "  Memory heaps: " << memProperties.memoryHeapCount << "\n";

    if (memProperties.memoryTypeCount < 2 || memProperties.memoryHeapCount < 2) {
        TEST_LOG_ERROR() << "✗ Expected at least 2 memory types and 2 heaps\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Memory properties retrieved\n\n";

    // Step 7: Create device
    TEST_LOG_INFO() << "[7] Creating logical device...\n";

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
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Logical device created\n\n";

    // Step 8: Get device queue
    TEST_LOG_INFO() << "[8] Getting device queue...\n";
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    if (queue == VK_NULL_HANDLE) {
        TEST_LOG_ERROR() << "✗ vkGetDeviceQueue returned NULL\n";
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✓ Device queue retrieved (handle: " << queue << ")\n\n";

    // Step 9: Destroy device
    TEST_LOG_INFO() << "[9] Destroying device...\n";
    vkDestroyDevice(device, nullptr);
    TEST_LOG_INFO() << "✓ Device destroyed\n\n";

    // Step 10: Destroy instance
    TEST_LOG_INFO() << "[10] Destroying instance...\n";
    vkDestroyInstance(instance, nullptr);
    TEST_LOG_INFO() << "✓ Instance destroyed\n\n";

    TEST_LOG_INFO() << "========================================\n";
    TEST_LOG_INFO() << "✓ Phase 3 PASSED\n";
    TEST_LOG_INFO() << "========================================\n\n";

    return true;
}
