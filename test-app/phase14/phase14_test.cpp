#include "phase14_test.h"

#include "logging.h"

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

namespace {

struct InstanceBundle {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
};

struct DeviceBundle {
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
};

bool create_instance(InstanceBundle* bundle) {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Phase 14 Feature/Property Queries";
    app.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    return vkCreateInstance(&info, nullptr, &bundle->instance) == VK_SUCCESS;
}

bool pick_physical(InstanceBundle* bundle) {
    uint32_t count = 0;
    VkResult res = vkEnumeratePhysicalDevices(bundle->instance, &count, nullptr);
    if (res != VK_SUCCESS || count == 0) {
        TEST_LOG_ERROR() << "✗ No physical devices available";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    res = vkEnumeratePhysicalDevices(bundle->instance, &count, devices.data());
    if (res != VK_SUCCESS || devices.empty()) {
        TEST_LOG_ERROR() << "✗ Failed to enumerate physical devices";
        return false;
    }
    bundle->physical = devices[0];
    return true;
}

void query_vulkan14_features(VkPhysicalDevice physical,
                             VkPhysicalDeviceVulkan14Features* out_vk14,
                             VkPhysicalDeviceGlobalPriorityQueryFeatures* out_global_priority,
                             VkPhysicalDeviceLineRasterizationFeaturesEXT* out_line_feats,
                             VkPhysicalDeviceDynamicRenderingLocalReadFeatures* out_dyn_read) {
    VkPhysicalDeviceGlobalPriorityQueryFeatures gpq = {};
    gpq.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES;
    VkPhysicalDeviceDynamicRenderingLocalReadFeatures dyn_read = {};
    dyn_read.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES;
    VkPhysicalDeviceLineRasterizationFeaturesEXT line_feats = {};
    line_feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
    VkPhysicalDeviceVulkan14Features vk14 = {};
    vk14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    gpq.pNext = &dyn_read;
    dyn_read.pNext = &line_feats;
    line_feats.pNext = &vk14;

    VkPhysicalDeviceFeatures2 feats2 = {};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &gpq;
    vkGetPhysicalDeviceFeatures2(physical, &feats2);

    if (out_vk14) {
        *out_vk14 = vk14;
    }
    if (out_global_priority) {
        *out_global_priority = gpq;
    }
    if (out_line_feats) {
        *out_line_feats = line_feats;
    }
    if (out_dyn_read) {
        *out_dyn_read = dyn_read;
    }
}

bool query_vulkan14_properties(VkPhysicalDevice physical,
                               VkPhysicalDeviceVulkan14Properties* out_props) {
    std::array<VkImageLayout, 8> src_layouts = {};
    std::array<VkImageLayout, 8> dst_layouts = {};
    VkPhysicalDeviceVulkan14Properties props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
    props.copySrcLayoutCount = static_cast<uint32_t>(src_layouts.size());
    props.pCopySrcLayouts = src_layouts.data();
    props.copyDstLayoutCount = static_cast<uint32_t>(dst_layouts.size());
    props.pCopyDstLayouts = dst_layouts.data();

    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &props;

    vkGetPhysicalDeviceProperties2(physical, &props2);

    if (props.copySrcLayoutCount > src_layouts.size() ||
        props.copyDstLayoutCount > dst_layouts.size()) {
        TEST_LOG_WARN() << "⚠️ Layout count overflow (src=" << props.copySrcLayoutCount
                        << ", dst=" << props.copyDstLayoutCount << ")";
        return false;
    }

    if (out_props) {
        *out_props = props;
    }
    return true;
}

uint32_t pick_graphics_queue_family(VkPhysicalDevice physical) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool create_device_with_usage2(VkPhysicalDevice physical,
                               const VkPhysicalDeviceVulkan14Features& vk14_feats,
                               DeviceBundle* device_bundle) {
    if (!vk14_feats.maintenance5) {
        TEST_LOG_WARN() << "⚠️ maintenance5 not supported, skipping buffer usage2 check";
        return true;
    }

    uint32_t family_index = pick_graphics_queue_family(physical);
    if (family_index == UINT32_MAX) {
        TEST_LOG_WARN() << "⚠️ No graphics queue family, skipping";
        return true;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = family_index;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan14Features feats_enable = vk14_feats;
    feats_enable.maintenance5 = VK_TRUE;

    VkPhysicalDeviceFeatures2 feats2 = {};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &feats_enable;

    VkDeviceCreateInfo dev_info = {};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &queue_info;
    dev_info.pNext = &feats2;

    VkResult res = vkCreateDevice(physical, &dev_info, nullptr, &device_bundle->device);
    if (res != VK_SUCCESS) {
        TEST_LOG_WARN() << "⚠️ vkCreateDevice failed with maintenance5 enabled: " << res;
        return true;
    }

    device_bundle->queue_family = family_index;
    vkGetDeviceQueue(device_bundle->device, family_index, 0, &device_bundle->queue);

    VkBufferUsageFlags2CreateInfo usage2 = {};
    usage2.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO;
    usage2.usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT;

    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = 256;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    buf_info.pNext = &usage2;

    VkBuffer buffer = VK_NULL_HANDLE;
    res = vkCreateBuffer(device_bundle->device, &buf_info, nullptr, &buffer);
    if (res != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateBuffer with usage2 failed: " << res;
        return false;
    }

    vkDestroyBuffer(device_bundle->device, buffer, nullptr);
    return true;
}

void destroy_device(DeviceBundle* bundle) {
    if (bundle->device) {
        vkDestroyDevice(bundle->device, nullptr);
        bundle->device = VK_NULL_HANDLE;
        bundle->queue = VK_NULL_HANDLE;
    }
}

} // namespace

bool run_phase14_test() {
    TEST_LOG_INFO() << "Running Phase 14: Vulkan 1.4 feature/property plumbing";

    InstanceBundle instance_bundle;
    if (!create_instance(&instance_bundle)) {
        TEST_LOG_ERROR() << "✗ Failed to create instance";
        return false;
    }

    if (!pick_physical(&instance_bundle)) {
        vkDestroyInstance(instance_bundle.instance, nullptr);
        return false;
    }

    VkPhysicalDeviceVulkan14Features vk14_feats = {};
    VkPhysicalDeviceGlobalPriorityQueryFeatures gpq_feats = {};
    VkPhysicalDeviceLineRasterizationFeaturesEXT line_feats = {};
    VkPhysicalDeviceDynamicRenderingLocalReadFeatures dyn_read_feats = {};
    query_vulkan14_features(instance_bundle.physical,
                            &vk14_feats,
                            &gpq_feats,
                            &line_feats,
                            &dyn_read_feats);

    VkPhysicalDeviceVulkan14Properties vk14_props = {};
    if (!query_vulkan14_properties(instance_bundle.physical, &vk14_props)) {
        TEST_LOG_ERROR() << "✗ vkGetPhysicalDeviceProperties2 validation failed";
        vkDestroyInstance(instance_bundle.instance, nullptr);
        return false;
    }

    if (vk14_props.copySrcLayoutCount == 0 || vk14_props.copyDstLayoutCount == 0 ||
        !vk14_props.pCopySrcLayouts || !vk14_props.pCopyDstLayouts) {
        TEST_LOG_ERROR() << "✗ Vulkan1.4 copy layout lists not populated";
        vkDestroyInstance(instance_bundle.instance, nullptr);
        return false;
    }

    if (vk14_props.pCopySrcLayouts[0] == VK_IMAGE_LAYOUT_UNDEFINED ||
        vk14_props.pCopyDstLayouts[0] == VK_IMAGE_LAYOUT_UNDEFINED) {
        TEST_LOG_ERROR() << "✗ Vulkan1.4 copy layouts contain UNDEFINED entries";
        vkDestroyInstance(instance_bundle.instance, nullptr);
        return false;
    }

    if (!vk14_feats.hostImageCopy || !vk14_feats.maintenance6 || !vk14_feats.pushDescriptor ||
        !vk14_feats.pipelineRobustness || !vk14_feats.pipelineProtectedAccess ||
        !vk14_feats.dynamicRenderingLocalRead) {
        TEST_LOG_ERROR() << "✗ Vulkan1.4 feature flags missing (hostImageCopy/maintenance6/pushDescriptor/pipelineRobustness/protectedAccess/dynamicRenderingLocalRead)";
        vkDestroyInstance(instance_bundle.instance, nullptr);
        return false;
    }

    if (gpq_feats.globalPriorityQuery) {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(instance_bundle.physical, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties2> qf_props(qf_count);
        std::vector<VkQueueFamilyGlobalPriorityProperties> gp_props(qf_count);
        for (uint32_t i = 0; i < qf_count; ++i) {
            qf_props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            gp_props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES;
            qf_props[i].pNext = &gp_props[i];
        }
        vkGetPhysicalDeviceQueueFamilyProperties2(instance_bundle.physical, &qf_count, qf_props.data());
        bool any_priority = false;
        for (const auto& gp : gp_props) {
            if (gp.priorityCount > 0) {
                any_priority = true;
                break;
            }
        }
        if (!any_priority) {
            TEST_LOG_ERROR() << "✗ globalPriorityQuery supported but no priorities reported";
            vkDestroyInstance(instance_bundle.instance, nullptr);
            return false;
        }
    } else {
        TEST_LOG_WARN() << "⚠️ globalPriorityQuery not supported, skipping priority property checks";
    }

    TEST_LOG_INFO() << "Vulkan1.4 hostImageCopy: " << (vk14_feats.hostImageCopy ? "YES" : "NO");
    TEST_LOG_INFO() << "Vulkan1.4 maintenance5: " << (vk14_feats.maintenance5 ? "YES" : "NO");
    TEST_LOG_INFO() << "Vulkan1.4 maintenance6: " << (vk14_feats.maintenance6 ? "YES" : "NO");
    TEST_LOG_INFO() << "Global priority query: " << (gpq_feats.globalPriorityQuery ? "YES" : "NO");
    TEST_LOG_INFO() << "Line rasterization features: rect="
                    << (line_feats.rectangularLines ? "Y" : "N") << " stipple="
                    << (line_feats.stippledRectangularLines ? "Y" : "N");
    TEST_LOG_INFO() << "Dynamic rendering local read: " << (dyn_read_feats.dynamicRenderingLocalRead ? "YES" : "NO");
    TEST_LOG_INFO() << "Pipeline robustness flag: " << (vk14_feats.pipelineRobustness ? "YES" : "NO")
                    << " protected: " << (vk14_feats.pipelineProtectedAccess ? "YES" : "NO");

    DeviceBundle device_bundle;
    bool device_ok = create_device_with_usage2(instance_bundle.physical, vk14_feats, &device_bundle);

    destroy_device(&device_bundle);
    vkDestroyInstance(instance_bundle.instance, nullptr);

    if (!device_ok) {
        return false;
    }

    TEST_LOG_INFO() << "Phase 14 PASSED";
    return true;
}
