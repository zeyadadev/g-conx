#include "phase04_test.h"

#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

int32_t find_memory_type(uint32_t type_bits,
                         VkMemoryPropertyFlags required,
                         const VkPhysicalDeviceMemoryProperties& properties) {
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void destroy_resource_chain(VkDevice device,
                            VkBuffer buffer,
                            VkDeviceMemory buffer_memory,
                            VkImage image,
                            VkDeviceMemory image_memory) {
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image, nullptr);
    }
    if (image_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image_memory, nullptr);
    }
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
    if (buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer_memory, nullptr);
    }
}

} // namespace

bool run_phase04_test() {
    std::cout << "\n========================================\n";
    std::cout << "Phase 4: Fake Resource Management\n";
    std::cout << "========================================\n\n";

    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;

    // Create instance
    std::cout << "[1] Creating instance...\n";
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 4 Test";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    std::cout << "✅ Instance created\n\n";

    // Enumerate physical devices
    uint32_t device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        std::cerr << "✗ Failed to enumerate physical devices\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        std::cerr << "✗ Failed to enumerate physical devices (second call)\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    physical_device = devices[0];
    std::cout << "[2] Selected physical device: " << physical_device << "\n\n";

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    // Create logical device
    std::cout << "[3] Creating device...\n";
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateDevice failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    std::cout << "✅ Device created\n\n";

    bool success = true;
    VkBufferCreateInfo buffer_info = {};
    VkMemoryRequirements buffer_requirements = {};
    VkMemoryRequirements image_requirements = {};
    VkMemoryAllocateInfo buffer_alloc = {};
    VkMemoryAllocateInfo image_alloc = {};
    VkImageCreateInfo image_info = {};
    VkImageSubresource subresource = {};
    VkSubresourceLayout layout = {};
    int32_t host_visible_index = -1;
    int32_t device_local_index = -1;
    const VkDeviceSize buffer_size = 1024ull * 1024ull; // 1MB

    std::cout << "[4] Creating buffer (" << buffer_size << " bytes)...\n";
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateBuffer failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ vkCreateBuffer succeeded\n";

    vkGetBufferMemoryRequirements(device, buffer, &buffer_requirements);
    std::cout << "   Requirements -> size=" << buffer_requirements.size
              << ", alignment=" << buffer_requirements.alignment << "\n";

    host_visible_index =
        find_memory_type(buffer_requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         memory_properties);
    if (host_visible_index < 0) {
        std::cerr << "✗ Unable to find host visible memory type for buffer\n";
        success = false;
        goto cleanup;
    }

    buffer_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buffer_alloc.allocationSize = buffer_requirements.size;
    buffer_alloc.memoryTypeIndex = static_cast<uint32_t>(host_visible_index);

    result = vkAllocateMemory(device, &buffer_alloc, nullptr, &buffer_memory);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkAllocateMemory (buffer) failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ Buffer memory allocated (type=" << host_visible_index << ")\n";

    result = vkBindBufferMemory(device, buffer, buffer_memory, 0);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkBindBufferMemory failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ Buffer bound to memory\n\n";

    std::cout << "[5] Creating image (256x256 RGBA)...\n";
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {256, 256, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateImage(device, &image_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkCreateImage failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ vkCreateImage succeeded\n";

    vkGetImageMemoryRequirements(device, image, &image_requirements);
    std::cout << "   Requirements -> size=" << image_requirements.size
              << ", alignment=" << image_requirements.alignment << "\n";

    device_local_index =
        find_memory_type(image_requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         memory_properties);
    if (device_local_index < 0) {
        std::cerr << "✗ Unable to find device local memory type for image\n";
        success = false;
        goto cleanup;
    }

    image_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    image_alloc.allocationSize = image_requirements.size;
    image_alloc.memoryTypeIndex = static_cast<uint32_t>(device_local_index);

    result = vkAllocateMemory(device, &image_alloc, nullptr, &image_memory);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkAllocateMemory (image) failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ Image memory allocated (type=" << device_local_index << ")\n";

    result = vkBindImageMemory(device, image, image_memory, 0);
    if (result != VK_SUCCESS) {
        std::cerr << "✗ vkBindImageMemory failed: " << result << "\n";
        success = false;
        goto cleanup;
    }
    std::cout << "✅ Image bound to memory\n";

    // Query subresource layout for completeness
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = 0;
    subresource.arrayLayer = 0;
    vkGetImageSubresourceLayout(device, image, &subresource, &layout);
    std::cout << "   Subresource layout -> offset=" << layout.offset
              << ", rowPitch=" << layout.rowPitch << "\n\n";

cleanup:
    std::cout << "[6] Cleaning up resources...\n";
    destroy_resource_chain(device, buffer, buffer_memory, image, image_memory);

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    if (success) {
        std::cout << "✅ Resource cleanup complete\n";
        std::cout << "\n========================================\n";
        std::cout << "Phase 4 PASSED\n";
        std::cout << "========================================\n\n";
    } else {
        std::cout << "✗ Phase 4 FAILED\n\n";
    }
    return success;
}
