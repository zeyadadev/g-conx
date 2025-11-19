#include "branding.h"

#include <string.h>

static const VPBrandingInfo kBrandingInfo = {
    .api_version = VK_API_VERSION_1_3,
    .driver_version = VK_MAKE_VERSION(1, 0, 0),
    .vendor_id = 0x1AF4,
    .device_id = 0x1050,
    .device_type = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
    .driver_id = VK_DRIVER_ID_MESA_VENUS,
    .conformance_version = {1, 3, 0, 0},
    .device_name = "Venus Plus Network GPU",
    .driver_name = "Venus Plus",
    .driver_info = "Network GPU v1.0.0",
};

static void fill_uuid_from_seed(const char* seed, uint8_t uuid[VK_UUID_SIZE]) {
    memset(uuid, 0, VK_UUID_SIZE);
    if (!seed) {
        return;
    }
    size_t len = strlen(seed);
    if (len > VK_UUID_SIZE) {
        len = VK_UUID_SIZE;
    }
    memcpy(uuid, seed, len);
}

static void apply_driver_properties(VkPhysicalDeviceDriverProperties* driver) {
    if (!driver) {
        return;
    }
    const VPBrandingInfo* branding = vp_get_branding_info();
    driver->driverID = branding->driver_id;
    strncpy(driver->driverName, branding->driver_name, VK_MAX_DRIVER_NAME_SIZE - 1);
    driver->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
    strncpy(driver->driverInfo, branding->driver_info, VK_MAX_DRIVER_INFO_SIZE - 1);
    driver->driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
    driver->conformanceVersion = branding->conformance_version;
}

static void apply_id_properties(VkPhysicalDeviceIDProperties* id_props) {
    if (!id_props) {
        return;
    }
    vp_branding_get_device_uuid(id_props->deviceUUID);
    vp_branding_get_driver_uuid(id_props->driverUUID);
    memset(id_props->deviceLUID, 0, sizeof(id_props->deviceLUID));
    id_props->deviceNodeMask = 0;
    id_props->deviceLUIDValid = VK_FALSE;
}

static void apply_vulkan11_properties(VkPhysicalDeviceVulkan11Properties* props) {
    if (!props) {
        return;
    }
    vp_branding_get_device_uuid(props->deviceUUID);
    vp_branding_get_driver_uuid(props->driverUUID);
    memset(props->deviceLUID, 0, sizeof(props->deviceLUID));
    props->deviceNodeMask = 0;
    props->deviceLUIDValid = VK_FALSE;
}

static void apply_vulkan12_properties(VkPhysicalDeviceVulkan12Properties* props) {
    if (!props) {
        return;
    }
    const VPBrandingInfo* branding = vp_get_branding_info();
    props->driverID = branding->driver_id;
    strncpy(props->driverName, branding->driver_name, VK_MAX_DRIVER_NAME_SIZE - 1);
    props->driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
    strncpy(props->driverInfo, branding->driver_info, VK_MAX_DRIVER_INFO_SIZE - 1);
    props->driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
    props->conformanceVersion = branding->conformance_version;
}

static void apply_pnext_properties(void* pnext_head) {
    VkBaseOutStructure* next = (VkBaseOutStructure*)pnext_head;
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            apply_driver_properties((VkPhysicalDeviceDriverProperties*)next);
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES) {
            apply_id_properties((VkPhysicalDeviceIDProperties*)next);
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES) {
            apply_vulkan11_properties((VkPhysicalDeviceVulkan11Properties*)next);
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES) {
            apply_vulkan12_properties((VkPhysicalDeviceVulkan12Properties*)next);
        }
        next = next->pNext;
    }
}

const VPBrandingInfo* vp_get_branding_info(void) {
    return &kBrandingInfo;
}

void vp_branding_get_pipeline_uuid(uint8_t uuid[VK_UUID_SIZE]) {
    fill_uuid_from_seed("VENUS-PLUS-PIPELINE", uuid);
}

void vp_branding_get_device_uuid(uint8_t uuid[VK_UUID_SIZE]) {
    fill_uuid_from_seed("VENUS-PLUS-DEVICE", uuid);
}

void vp_branding_get_driver_uuid(uint8_t uuid[VK_UUID_SIZE]) {
    fill_uuid_from_seed("VENUS-PLUS-DRIVER", uuid);
}

void vp_branding_apply_properties(VkPhysicalDeviceProperties* props) {
    if (!props) {
        return;
    }
    const VPBrandingInfo* branding = vp_get_branding_info();
    props->apiVersion = branding->api_version;
    props->driverVersion = branding->driver_version;
    props->vendorID = branding->vendor_id;
    props->deviceID = branding->device_id;
    props->deviceType = branding->device_type;
    strncpy(props->deviceName, branding->device_name, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    props->deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1] = '\0';
    vp_branding_get_pipeline_uuid(props->pipelineCacheUUID);
}

void vp_branding_apply_properties2(VkPhysicalDeviceProperties2* props2) {
    if (!props2) {
        return;
    }
    vp_branding_apply_properties(&props2->properties);
    apply_pnext_properties(props2->pNext);
}
