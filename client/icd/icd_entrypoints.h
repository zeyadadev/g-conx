#ifndef VENUS_PLUS_ICD_ENTRYPOINTS_H
#define VENUS_PLUS_ICD_ENTRYPOINTS_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

extern "C" {

// ICD interface (required for loader to recognize the ICD)
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

// Global commands
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion);
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);

} // extern "C"

#endif // VENUS_PLUS_ICD_ENTRYPOINTS_H
