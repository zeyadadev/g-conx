#include "phase09_1_test.h"

#include "logging.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

constexpr VkDeviceSize kMapTestSize = 4096;
constexpr uint32_t kMapPattern = 0xdeadbeefu;

bool create_instance(VkInstance* instance) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 9.1 Test";
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

uint32_t select_queue_family(VkPhysicalDevice physical_device) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }
    return 0;
}

uint32_t find_memory_type(uint32_t type_mask,
                          VkMemoryPropertyFlags desired,
                          const VkPhysicalDeviceMemoryProperties& props) {
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_mask & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & desired) == desired) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool test_extension_enumeration(VkPhysicalDevice physical_device) {
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        TEST_LOG_ERROR() << "✗ vkEnumerateDeviceExtensionProperties reported no extensions";
        return false;
    }
    std::vector<VkExtensionProperties> props(count);
    result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, props.data());
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkEnumerateDeviceExtensionProperties failed: " << result;
        return false;
    }
    TEST_LOG_INFO() << "✅ Device exposes " << count << " extensions";
    TEST_LOG_INFO() << "   First few:";
    for (uint32_t i = 0; i < std::min<uint32_t>(count, 3); ++i) {
        TEST_LOG_INFO() << "     • " << props[i].extensionName << " (rev " << props[i].specVersion << ")";
    }
    return true;
}

bool test_properties_and_features(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceDriverProperties driver_props = {};
    driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driver_props;

    vkGetPhysicalDeviceProperties2(physical_device, &props2);
    TEST_LOG_INFO() << "✅ vkGetPhysicalDeviceProperties2: device=" << props2.properties.deviceName
                    << " driver=" << driver_props.driverName;

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physical_device, &features2);
    TEST_LOG_INFO() << "✅ vkGetPhysicalDeviceFeatures2: samplerMirrorClampToEdge="
                    << (features12.samplerMirrorClampToEdge ? "supported" : "not supported");

    VkPhysicalDeviceMemoryProperties2 mem_props2 = {};
    mem_props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2(physical_device, &mem_props2);
    TEST_LOG_INFO() << "✅ vkGetPhysicalDeviceMemoryProperties2: heaps="
                    << mem_props2.memoryProperties.memoryHeapCount;
    return true;
}

bool create_device(VkPhysicalDevice physical_device,
                   uint32_t queue_family_index,
                   VkDevice* out_device) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = queue_family_index;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue;

    VkResult result = vkCreateDevice(physical_device, &device_info, nullptr, out_device);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << result;
        return false;
    }
    return true;
}

bool memory_round_trip(VkDevice device,
                       VkPhysicalDevice physical_device,
                       const VkPhysicalDeviceMemoryProperties& mem_props,
                       VkBuffer* out_buffer,
                       VkDeviceMemory* out_memory) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = kMapTestSize;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &buffer_info, nullptr, out_buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer (map test) failed: " << result;
        return false;
    }

    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, *out_buffer, &reqs);

    uint32_t type_index = find_memory_type(reqs.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           mem_props);
    if (type_index == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ No HOST_VISIBLE memory type found";
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = reqs.size;
    alloc_info.memoryTypeIndex = type_index;

    result = vkAllocateMemory(device, &alloc_info, nullptr, out_memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory failed: " << result;
        return false;
    }

    vkBindBufferMemory(device, *out_buffer, *out_memory, 0);

    void* ptr = nullptr;
    result = vkMapMemory(device, *out_memory, 0, kMapTestSize, 0, &ptr);
    if (result != VK_SUCCESS || !ptr) {
        TEST_LOG_ERROR() << "✗ vkMapMemory failed: " << result;
        return false;
    }
    uint32_t* data = static_cast<uint32_t*>(ptr);
    for (uint32_t i = 0; i < kMapTestSize / sizeof(uint32_t); ++i) {
        data[i] = kMapPattern + i;
    }
    vkUnmapMemory(device, *out_memory);
    TEST_LOG_INFO() << "✅ Wrote " << (kMapTestSize / sizeof(uint32_t)) << " uint32 values via Map/Unmap";

    // Read back and verify.
    result = vkMapMemory(device, *out_memory, 0, kMapTestSize, 0, &ptr);
    if (result != VK_SUCCESS || !ptr) {
        TEST_LOG_ERROR() << "✗ vkMapMemory (readback) failed: " << result;
        return false;
    }
    data = static_cast<uint32_t*>(ptr);
    bool ok = true;
    for (uint32_t i = 0; i < kMapTestSize / sizeof(uint32_t); ++i) {
        if (data[i] != kMapPattern + i) {
            TEST_LOG_ERROR() << "✗ Readback mismatch at " << i << ": got 0x" << std::hex << data[i]
                             << " expected 0x" << (kMapPattern + i);
            ok = false;
            break;
        }
    }
    vkUnmapMemory(device, *out_memory);
    if (ok) {
        TEST_LOG_INFO() << "✅ Memory readback matched pattern";
    }
    return ok;
}

bool test_image_and_sampler(VkDevice device,
                            VkPhysicalDevice physical_device,
                            const VkPhysicalDeviceMemoryProperties& mem_props) {
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {4, 4, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(device, &image_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateImage failed: " << result;
        return false;
    }

    VkMemoryRequirements image_reqs = {};
    vkGetImageMemoryRequirements(device, image, &image_reqs);
    uint32_t image_type = find_memory_type(image_reqs.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           mem_props);
    if (image_type == UINT32_MAX) {
        image_type = find_memory_type(image_reqs.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                      mem_props);
    }
    if (image_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ No suitable memory type for image";
        vkDestroyImage(device, image, nullptr);
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = image_reqs.size;
    alloc_info.memoryTypeIndex = image_type;

    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(device, &alloc_info, nullptr, &image_memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory (image) failed: " << result;
        vkDestroyImage(device, image, nullptr);
        return false;
    }
    vkBindImageMemory(device, image, image_memory, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_info.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view = VK_NULL_HANDLE;
    result = vkCreateImageView(device, &view_info, nullptr, &image_view);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateImageView failed: " << result;
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        return false;
    }

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 256;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

    VkBuffer texel_buffer = VK_NULL_HANDLE;
    result = vkCreateBuffer(device, &buffer_info, nullptr, &texel_buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer (texel) failed: " << result;
        vkDestroyImageView(device, image_view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        return false;
    }

    VkMemoryRequirements buffer_reqs = {};
    vkGetBufferMemoryRequirements(device, texel_buffer, &buffer_reqs);
    uint32_t buffer_type = find_memory_type(buffer_reqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                            mem_props);
    if (buffer_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ No HOST_VISIBLE type for buffer view";
        vkDestroyBuffer(device, texel_buffer, nullptr);
        vkDestroyImageView(device, image_view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        return false;
    }

    alloc_info.allocationSize = buffer_reqs.size;
    alloc_info.memoryTypeIndex = buffer_type;

    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory (buffer view) failed: " << result;
        vkDestroyBuffer(device, texel_buffer, nullptr);
        vkDestroyImageView(device, image_view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        return false;
    }

    vkBindBufferMemory(device, texel_buffer, buffer_memory, 0);

    VkBufferViewCreateInfo buffer_view_info = {};
    buffer_view_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    buffer_view_info.buffer = texel_buffer;
    buffer_view_info.format = VK_FORMAT_R32_SFLOAT;
    buffer_view_info.offset = 0;
    buffer_view_info.range = VK_WHOLE_SIZE;

    VkBufferView buffer_view = VK_NULL_HANDLE;
    result = vkCreateBufferView(device, &buffer_view_info, nullptr, &buffer_view);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBufferView failed: " << result;
        vkFreeMemory(device, buffer_memory, nullptr);
        vkDestroyBuffer(device, texel_buffer, nullptr);
        vkDestroyImageView(device, image_view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        return false;
    }

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    result = vkCreateSampler(device, &sampler_info, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateSampler failed: " << result;
    } else {
        TEST_LOG_INFO() << "✅ Created sampler, image view, and buffer view successfully";
        vkDestroySampler(device, sampler, nullptr);
    }

    vkDestroyBufferView(device, buffer_view, nullptr);
    vkFreeMemory(device, buffer_memory, nullptr);
    vkDestroyBuffer(device, texel_buffer, nullptr);
    vkDestroyImageView(device, image_view, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, image_memory, nullptr);
    return result == VK_SUCCESS;
}

} // namespace

bool run_phase09_1_test() {
    TEST_LOG_INFO() << "\n========================================\n";
    TEST_LOG_INFO() << "Phase 9.1: Compute Application Compatibility\n";
    TEST_LOG_INFO() << "========================================\n";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer map_buffer = VK_NULL_HANDLE;
    VkDeviceMemory map_memory = VK_NULL_HANDLE;
    bool success = false;

    VkPhysicalDeviceMemoryProperties mem_props = {};

    do {
        if (!create_instance(&instance)) break;
        if (!pick_physical_device(instance, &physical_device)) break;

        if (!test_extension_enumeration(physical_device)) break;
        if (!test_properties_and_features(physical_device)) break;

        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
        uint32_t queue_family = select_queue_family(physical_device);
        if (!create_device(physical_device, queue_family, &device)) break;

        if (!memory_round_trip(device, physical_device, mem_props, &map_buffer, &map_memory)) break;
        if (!test_image_and_sampler(device, physical_device, mem_props)) break;

        success = true;
    } while (false);

    if (map_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, map_buffer, nullptr);
    }
    if (map_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, map_memory, nullptr);
    }
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }

    if (success) {
        TEST_LOG_INFO() << "✅ Phase 9.1 PASSED";
    } else {
        TEST_LOG_ERROR() << "✗ Phase 9.1 FAILED";
    }
    return success;
}
