#include "phase12_test.h"

#include "logging.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kWidth = 4;
constexpr uint32_t kHeight = 4;
constexpr uint32_t kQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

struct DeviceHandles {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
};

uint32_t find_queue_family(VkPhysicalDevice physical) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & kQueueFlags) {
            return i;
        }
    }
    return UINT32_MAX;
}

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

bool create_instance(DeviceHandles* handles) {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Phase 12 Host Image Copy";
    app.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    return vkCreateInstance(&info, nullptr, &handles->instance) == VK_SUCCESS;
}

bool pick_physical(DeviceHandles* handles) {
    uint32_t count = 0;
    VkResult res = vkEnumeratePhysicalDevices(handles->instance, &count, nullptr);
    if (res != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    res = vkEnumeratePhysicalDevices(handles->instance, &count, devices.data());
    if (res != VK_SUCCESS || devices.empty()) {
        return false;
    }
    handles->physical = devices[0];
    return true;
}

bool create_device(DeviceHandles* handles, VkImageTiling tiling) {
    (void)tiling;
    uint32_t queue_family = find_queue_family(handles->physical);
    if (queue_family == UINT32_MAX) {
        TEST_LOG_WARN() << "⚠️ No compatible queue family found, skipping";
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceHostImageCopyFeatures host_features = {};
    host_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES;

    VkPhysicalDeviceVulkan14Features vk14 = {};
    vk14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    host_features.pNext = &vk14;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &host_features;
    vkGetPhysicalDeviceFeatures2(handles->physical, &features2);

    if (!host_features.hostImageCopy && !vk14.hostImageCopy) {
        TEST_LOG_WARN() << "⚠️ hostImageCopy not supported, skipping";
        return false;
    }

    host_features.hostImageCopy = VK_TRUE;
    vk14.hostImageCopy = VK_TRUE;

    const char* extensions[] = {"VK_EXT_host_image_copy"};
    std::vector<const char*> enabled_exts;
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(handles->physical, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(handles->physical, nullptr, &ext_count, exts.data());
    for (const char* name : extensions) {
        for (const auto& ext : exts) {
            if (strcmp(ext.extensionName, name) == 0) {
                enabled_exts.push_back(name);
                break;
            }
        }
    }

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features2;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
    device_info.ppEnabledExtensionNames = enabled_exts.empty() ? nullptr : enabled_exts.data();

    VkResult res = vkCreateDevice(handles->physical, &device_info, nullptr, &handles->device);
    if (res != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << res;
        return false;
    }

    vkGetDeviceQueue(handles->device, queue_family, 0, &handles->queue);
    if (handles->queue == VK_NULL_HANDLE) {
        TEST_LOG_ERROR() << "✗ vkGetDeviceQueue returned NULL";
        return false;
    }
    return true;
}

void destroy_handles(DeviceHandles* handles) {
    if (handles->device) {
        vkDeviceWaitIdle(handles->device);
        vkDestroyDevice(handles->device, nullptr);
    }
    if (handles->instance) {
        vkDestroyInstance(handles->instance, nullptr);
    }
    *handles = {};
}

} // namespace

struct HostFormatChoice {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    uint32_t bytes_per_pixel = 0;
};

HostFormatChoice choose_host_format(VkPhysicalDevice physical) {
    const VkFormat candidates[] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8_UNORM,
    };

    for (VkFormat fmt : candidates) {
        VkFormatProperties3 props3 = {};
        props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
        VkFormatProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
        props2.pNext = &props3;
        vkGetPhysicalDeviceFormatProperties2(physical, fmt, &props2);

        auto has_host = [&](VkFormatFeatureFlags2 feats) {
            return (feats & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) != 0 &&
                   (feats & VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT) != 0 &&
                   (feats & VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT) != 0;
        };

        VkImageTiling chosen_tiling = VK_IMAGE_TILING_MAX_ENUM;
        if (has_host(props3.optimalTilingFeatures)) {
            chosen_tiling = VK_IMAGE_TILING_OPTIMAL;
        } else if (has_host(props3.linearTilingFeatures)) {
            chosen_tiling = VK_IMAGE_TILING_LINEAR;
        } else {
            continue;
        }

        uint32_t bpp = 0;
        switch (fmt) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SRGB: bpp = 1; break;
        case VK_FORMAT_R8G8_UNORM: bpp = 2; break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_SRGB: bpp = 4; break;
        default: bpp = 0; break;
        }
        if (bpp == 0) {
            continue;
        }

        HostFormatChoice choice = {};
        choice.format = fmt;
        choice.tiling = chosen_tiling;
        choice.bytes_per_pixel = bpp;
        return choice;
    }

    return {};
}

bool run_phase12_test() {
    TEST_LOG_INFO() << "\n========================================";
    TEST_LOG_INFO() << "Phase 12: Host Image Copy";
    TEST_LOG_INFO() << "========================================\n";

    DeviceHandles handles = {};
    HostFormatChoice host_format = {};
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool success = false;

    do {
        if (!create_instance(&handles)) {
            TEST_LOG_ERROR() << "✗ vkCreateInstance failed";
            break;
        }
        if (!pick_physical(&handles)) {
            TEST_LOG_ERROR() << "✗ Failed to pick physical device";
            break;
        }

        host_format = choose_host_format(handles.physical);
        if (host_format.format == VK_FORMAT_UNDEFINED || host_format.bytes_per_pixel == 0) {
            TEST_LOG_WARN() << "⚠️ No format with HOST_IMAGE_TRANSFER support, skipping";
            success = true;
            break;
        }

        if (!create_device(&handles, host_format.tiling)) {
            break;
        }

        VkPhysicalDeviceMemoryProperties mem_props = {};
        vkGetPhysicalDeviceMemoryProperties(handles.physical, &mem_props);

        VkImageCreateInfo img_info = {};
        img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = host_format.format;
        img_info.extent = {kWidth, kHeight, 1};
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling = host_format.tiling;
        img_info.usage = VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult res = vkCreateImage(handles.device, &img_info, nullptr, &image);
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCreateImage failed: " << res;
            break;
        }

        VkMemoryRequirements reqs = {};
        vkGetImageMemoryRequirements(handles.device, image, &reqs);
        uint32_t mem_type = UINT32_MAX;
        if (reqs.memoryTypeBits == 0) {
            if (mem_props.memoryTypeCount == 0) {
                TEST_LOG_WARN() << "⚠️ No memory types available, skipping";
                success = true;
                break;
            }
            TEST_LOG_WARN() << "⚠️ memoryTypeBits reported as 0, falling back to type 0";
            mem_type = 0;
        } else {
            // Prefer device-local; host copy does not require HOST_VISIBLE image memory.
            mem_type = find_memory_type(reqs.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                        mem_props);
            if (mem_type == UINT32_MAX) {
                mem_type = find_memory_type(reqs.memoryTypeBits, 0, mem_props);
            }
        }
        if (mem_type == UINT32_MAX) {
            TEST_LOG_WARN() << "⚠️ No usable memory type for image (bits=0x"
                            << std::hex << reqs.memoryTypeBits << std::dec << "), skipping";
            success = true;
            break;
        }

        VkMemoryAllocateInfo alloc = {};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = reqs.size;
        alloc.memoryTypeIndex = mem_type;
        res = vkAllocateMemory(handles.device, &alloc, nullptr, &memory);
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkAllocateMemory failed: " << res;
            break;
        }

        res = vkBindImageMemory(handles.device, image, memory, 0);
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkBindImageMemory failed: " << res;
            break;
        }

        VkImageSubresourceRange range = {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        VkHostImageLayoutTransitionInfo transition = {};
        transition.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO;
        transition.image = image;
        transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        transition.subresourceRange = range;

        res = vkTransitionImageLayout(handles.device, 1, &transition);
        if (res == VK_ERROR_EXTENSION_NOT_PRESENT || res == VK_ERROR_FEATURE_NOT_PRESENT) {
            TEST_LOG_WARN() << "⚠️ host image layout transition not supported by device, skipping";
            success = true;
            break;
        }
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkTransitionImageLayout failed: " << res;
            break;
        }

        const size_t pixel_bytes = static_cast<size_t>(kWidth) * kHeight * host_format.bytes_per_pixel;
        std::vector<uint8_t> pixels(pixel_bytes);
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                size_t idx = (y * kWidth + x) * host_format.bytes_per_pixel;
                switch (host_format.bytes_per_pixel) {
                case 1:
                    pixels[idx] = static_cast<uint8_t>(x * 7 + y);
                    break;
                case 2:
                    pixels[idx + 0] = static_cast<uint8_t>(x * 5 + y);
                    pixels[idx + 1] = static_cast<uint8_t>(y * 3);
                    break;
                case 4:
                    pixels[idx + 0] = static_cast<uint8_t>(x * 10 + y);
                    pixels[idx + 1] = static_cast<uint8_t>(x * 10 + 1);
                    pixels[idx + 2] = static_cast<uint8_t>(y * 3);
                    pixels[idx + 3] = 0xff;
                    break;
                default:
                    break;
                }
            }
        }

        VkMemoryToImageCopy mem_to_img = {};
        mem_to_img.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY;
        mem_to_img.pHostPointer = pixels.data();
        mem_to_img.memoryRowLength = 0;
        mem_to_img.memoryImageHeight = 0;
        mem_to_img.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        mem_to_img.imageOffset = {0, 0, 0};
        mem_to_img.imageExtent = {kWidth, kHeight, 1};

        VkCopyMemoryToImageInfo copy_to_info = {};
        copy_to_info.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO;
        copy_to_info.dstImage = image;
        copy_to_info.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copy_to_info.regionCount = 1;
        copy_to_info.pRegions = &mem_to_img;

        res = vkCopyMemoryToImage(handles.device, &copy_to_info);
        if (res == VK_ERROR_EXTENSION_NOT_PRESENT || res == VK_ERROR_FEATURE_NOT_PRESENT) {
            TEST_LOG_WARN() << "⚠️ host image copy-to not supported by device, skipping";
            success = true;
            break;
        }
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCopyMemoryToImage failed: " << res;
            break;
        }

        VkImageToMemoryCopy img_to_mem = {};
        img_to_mem.sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY;
        img_to_mem.pHostPointer = nullptr; // set below
        img_to_mem.memoryRowLength = 0;
        img_to_mem.memoryImageHeight = 0;
        img_to_mem.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        img_to_mem.imageOffset = {0, 0, 0};
        img_to_mem.imageExtent = {kWidth, kHeight, 1};

        std::vector<uint8_t> readback(pixel_bytes);
        img_to_mem.pHostPointer = readback.data();

        VkCopyImageToMemoryInfo copy_from_info = {};
        copy_from_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO;
        copy_from_info.srcImage = image;
        copy_from_info.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copy_from_info.regionCount = 1;
        copy_from_info.pRegions = &img_to_mem;

        res = vkCopyImageToMemory(handles.device, &copy_from_info);
        if (res == VK_ERROR_EXTENSION_NOT_PRESENT || res == VK_ERROR_FEATURE_NOT_PRESENT) {
            TEST_LOG_WARN() << "⚠️ host image copy-from not supported by device, skipping";
            success = true;
            break;
        }
        if (res != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkCopyImageToMemory failed: " << res;
            break;
        }

        if (pixels.size() != readback.size() ||
            memcmp(pixels.data(), readback.data(), readback.size()) != 0) {
            TEST_LOG_ERROR() << "✗ Host image copy data mismatch";
            break;
        }

        success = true;
    } while (false);

    if (image != VK_NULL_HANDLE && handles.device) {
        vkDestroyImage(handles.device, image, nullptr);
    }
    if (memory != VK_NULL_HANDLE && handles.device) {
        vkFreeMemory(handles.device, memory, nullptr);
    }
    destroy_handles(&handles);

    if (success) {
        TEST_LOG_INFO() << "Phase 12 PASSED";
    }
    return success;
}
