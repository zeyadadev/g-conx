#ifndef VENUS_PLUS_VULKAN_CONTEXT_H
#define VENUS_PLUS_VULKAN_CONTEXT_H

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace venus_plus {

struct VulkanContextCreateInfo {
    bool enable_validation = false;
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    bool initialize(const VulkanContextCreateInfo& info);
    void shutdown();

    bool is_initialized() const { return initialized_; }
    bool validation_enabled() const { return validation_enabled_; }

    VkInstance instance() const { return instance_; }
    VkDebugUtilsMessengerEXT debug_messenger() const { return debug_messenger_; }

private:
    bool create_instance();
    bool create_debug_messenger();

    bool initialized_ = false;
    bool validation_enabled_ = false;
    VulkanContextCreateInfo create_info_ = {};
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkInstanceCreateFlags instance_flags_ = 0;

    std::vector<std::string> owned_layer_names_;
    std::vector<const char*> layer_name_ptrs_;
    std::vector<std::string> owned_extension_names_;
    std::vector<const char*> extension_name_ptrs_;

    void populate_layer_list();
    void populate_extension_list();
    bool extension_available(const char* name, const std::vector<VkExtensionProperties>& props) const;
};

} // namespace venus_plus

#endif // VENUS_PLUS_VULKAN_CONTEXT_H
