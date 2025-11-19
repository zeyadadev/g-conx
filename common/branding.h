#ifndef VENUS_PLUS_BRANDING_H
#define VENUS_PLUS_BRANDING_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VPBrandingInfo {
    uint32_t api_version;
    uint32_t driver_version;
    uint32_t vendor_id;
    uint32_t device_id;
    VkPhysicalDeviceType device_type;
    VkDriverId driver_id;
    VkConformanceVersion conformance_version;
    const char* device_name;
    const char* driver_name;
    const char* driver_info;
} VPBrandingInfo;

const VPBrandingInfo* vp_get_branding_info(void);

void vp_branding_get_pipeline_uuid(uint8_t uuid[VK_UUID_SIZE]);
void vp_branding_get_device_uuid(uint8_t uuid[VK_UUID_SIZE]);
void vp_branding_get_driver_uuid(uint8_t uuid[VK_UUID_SIZE]);

void vp_branding_apply_properties(VkPhysicalDeviceProperties* props);
void vp_branding_apply_properties2(VkPhysicalDeviceProperties2* props2);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_BRANDING_H
