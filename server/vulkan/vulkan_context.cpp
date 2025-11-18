#include "vulkan_context.h"
#include "utils/logging.h"

#include <algorithm>
#include <cstring>
#include <vector>

#define VULKAN_LOG_ERROR() VP_LOG_STREAM_ERROR(VULKAN)
#define VULKAN_LOG_WARN() VP_LOG_STREAM_WARN(VULKAN)
#define VULKAN_LOG_INFO() VP_LOG_STREAM_INFO(VULKAN)

namespace venus_plus {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                              VkDebugUtilsMessageTypeFlagsEXT messageType,
                                              const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                              void* /*pUserData*/) {
    const char* severity = "INFO";
    venus_plus::LogLevel level = venus_plus::LogLevel::INFO;
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity = "ERROR";
        level = venus_plus::LogLevel::ERROR;
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity = "WARNING";
        level = venus_plus::LogLevel::WARN;
    }

    const char* type = "GENERAL";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        type = "VALIDATION";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        type = "PERF";
    }

    const char* message = callbackData && callbackData->pMessage ? callbackData->pMessage : "Unknown message";
    VP_LOG(level, venus_plus::LogCategory::VULKAN, "[Vulkan][%s][%s] %s", severity, type, message);
    return VK_FALSE;
}

} // namespace

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    shutdown();
}

bool VulkanContext::initialize(const VulkanContextCreateInfo& info) {
    if (initialized_) {
        return true;
    }

    create_info_ = info;
    validation_enabled_ = info.enable_validation;

    populate_layer_list();
    populate_extension_list();

    if (!create_instance()) {
        return false;
    }

    if (validation_enabled_) {
        if (!create_debug_messenger()) {
            VULKAN_LOG_ERROR() << "Failed to create debug messenger (validation enabled)";
        }
    }

    initialized_ = true;
    return true;
}

void VulkanContext::shutdown() {
    if (!initialized_) {
        return;
    }

    if (debug_messenger_ != VK_NULL_HANDLE) {
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn) {
            destroy_fn(instance_, debug_messenger_, nullptr);
        }
        debug_messenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

void VulkanContext::populate_layer_list() {
    owned_layer_names_.clear();
    layer_name_ptrs_.clear();

    if (!validation_enabled_) {
        return;
    }

    uint32_t layer_count = 0;
    if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS || layer_count == 0) {
        VULKAN_LOG_WARN() << "No instance layers available, disabling validation";
        validation_enabled_ = false;
        return;
    }

    std::vector<VkLayerProperties> layers(layer_count);
    if (vkEnumerateInstanceLayerProperties(&layer_count, layers.data()) != VK_SUCCESS) {
        VULKAN_LOG_WARN() << "Failed to enumerate instance layers, disabling validation";
        validation_enabled_ = false;
        return;
    }

    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    auto it = std::find_if(layers.begin(), layers.end(),
                           [validation_layer](const VkLayerProperties& props) {
                               return std::strcmp(props.layerName, validation_layer) == 0;
                           });

    if (it == layers.end()) {
        VULKAN_LOG_WARN() << "Validation layer not available, disabling validation";
        validation_enabled_ = false;
        return;
    }

    owned_layer_names_.push_back(validation_layer);
    layer_name_ptrs_.push_back(owned_layer_names_.back().c_str());
}

bool VulkanContext::extension_available(const char* name,
                                        const std::vector<VkExtensionProperties>& props) const {
    return std::any_of(props.begin(), props.end(),
                       [name](const VkExtensionProperties& prop) {
                           return std::strcmp(prop.extensionName, name) == 0;
                       });
}

void VulkanContext::populate_extension_list() {
    owned_extension_names_.clear();
    extension_name_ptrs_.clear();
    instance_flags_ = 0;

    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        VULKAN_LOG_ERROR() << "Failed to enumerate instance extensions";
        return;
    }

    std::vector<VkExtensionProperties> props(count);
    if (count > 0 &&
        vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) {
        VULKAN_LOG_ERROR() << "Failed to enumerate instance extensions (pass 2)";
        return;
    }

    if (validation_enabled_) {
        const char* debug_utils = "VK_EXT_debug_utils";
        if (extension_available(debug_utils, props)) {
            owned_extension_names_.push_back(debug_utils);
            extension_name_ptrs_.push_back(owned_extension_names_.back().c_str());
        } else {
            VULKAN_LOG_WARN() << "VK_EXT_debug_utils missing, disabling validation messenger";
        }
    }

    const char* portability_ext = "VK_KHR_portability_enumeration";
    if (extension_available(portability_ext, props)) {
        owned_extension_names_.push_back(portability_ext);
        extension_name_ptrs_.push_back(owned_extension_names_.back().c_str());
        instance_flags_ |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
}

bool VulkanContext::create_instance() {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Venus Plus Server";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = static_cast<uint32_t>(layer_name_ptrs_.size());
    create_info.ppEnabledLayerNames = layer_name_ptrs_.empty() ? nullptr : layer_name_ptrs_.data();
    create_info.enabledExtensionCount = static_cast<uint32_t>(extension_name_ptrs_.size());
    create_info.ppEnabledExtensionNames =
        extension_name_ptrs_.empty() ? nullptr : extension_name_ptrs_.data();
    create_info.flags = instance_flags_;

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        VULKAN_LOG_ERROR() << "vkCreateInstance failed: " << result;
        instance_ = VK_NULL_HANDLE;
        return false;
    }

    VULKAN_LOG_INFO() << "Instance created (validation="
                      << (validation_enabled_ ? "on" : "off") << ")";
    return true;
}

bool VulkanContext::create_debug_messenger() {
    if (!validation_enabled_) {
        return true;
    }

    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create_fn) {
        VULKAN_LOG_ERROR() << "vkCreateDebugUtilsMessengerEXT not found";
        return false;
    }

    VkDebugUtilsMessengerCreateInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;

    VkResult result = create_fn(instance_, &info, nullptr, &debug_messenger_);
    if (result != VK_SUCCESS) {
        VULKAN_LOG_ERROR() << "Failed to create debug messenger: " << result;
        debug_messenger_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace venus_plus
