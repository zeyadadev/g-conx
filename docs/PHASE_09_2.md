# Phase 9_2: GPU Branding

## Overview

Establish Venus Plus's identity by displaying custom virtual network GPU branding instead of forwarding the real GPU's identity. This creates a clear distinction that the application is using a network-based virtual GPU.

**Duration**: 1-2 days
**Prerequisites**: Phase 9_1 complete (performance improvements)

---

## Objectives

1. **Custom Device Identity**: Display "Venus Plus Network GPU" as device name
2. **Custom Driver Info**: Show Venus Plus driver name and version
3. **Custom Vendor/Device IDs**: Use virtual GPU identifiers
4. **Maintain Compatibility**: Keep real GPU limits and features for application compatibility

---

## Deliverables

- [ ] Branded `VkPhysicalDeviceProperties`
- [ ] Branded `VkPhysicalDeviceDriverProperties` (pNext chain)
- [ ] Custom pipeline cache UUID
- [ ] Updated `fake_gpu_data.cpp` with branding values

---

## Branding Specifications

### Device Properties

| Property | Current (Real GPU) | Branded Value |
|----------|-------------------|---------------|
| `deviceName` | "Quadro P1000" | "Venus Plus Network GPU" |
| `deviceType` | DISCRETE_GPU | VIRTUAL_GPU |
| `vendorID` | 0x10DE (NVIDIA) | 0x1AF4 (virtio) |
| `deviceID` | 0x1CB1 | 0x1050 |
| `driverVersion` | 580.95.5.0 | 1.0.0 |
| `apiVersion` | 1.4.312 | 1.3.0 (supported version) |

### Driver Properties

| Property | Branded Value |
|----------|---------------|
| `driverID` | `VK_DRIVER_ID_MESA_VENUS` |
| `driverName` | "Venus Plus" |
| `driverInfo` | "Network GPU v1.0.0" |
| `conformanceVersion` | 1.3.0.0 |

### What Stays Real

These are forwarded from the actual GPU to maintain compatibility:

- `VkPhysicalDeviceLimits` - All limits
- `VkPhysicalDeviceFeatures` - All features
- `VkPhysicalDeviceSparseProperties` - Sparse binding support
- Memory properties
- Queue family properties

---

## Implementation

### Part 1: Modify vkGetPhysicalDeviceProperties

**File**: `server/renderer_decoder.c`

```cpp
void server_dispatch_vkGetPhysicalDeviceProperties(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceProperties* args) {

    ServerState* state = (ServerState*)ctx->data;
    VkPhysicalDevice real_device = state->physical_device;

    // Get real properties (for limits)
    vkGetPhysicalDeviceProperties(real_device, args->pProperties);

    // Override with Venus Plus branding
    args->pProperties->apiVersion = VK_API_VERSION_1_3;
    args->pProperties->driverVersion = VK_MAKE_VERSION(1, 0, 0);
    args->pProperties->vendorID = 0x1AF4;  // virtio vendor
    args->pProperties->deviceID = 0x1050;  // Venus Plus device
    args->pProperties->deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;

    strncpy(args->pProperties->deviceName,
            "Venus Plus Network GPU",
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);

    // Generate deterministic UUID for pipeline cache
    generate_venus_plus_uuid(args->pProperties->pipelineCacheUUID);
}
```

### Part 2: Modify vkGetPhysicalDeviceProperties2

**File**: `server/renderer_decoder.c`

```cpp
void server_dispatch_vkGetPhysicalDeviceProperties2(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkGetPhysicalDeviceProperties2* args) {

    ServerState* state = (ServerState*)ctx->data;
    VkPhysicalDevice real_device = state->physical_device;

    // Get real properties2 (with pNext chain)
    vkGetPhysicalDeviceProperties2(real_device, args->pProperties);

    // Override base properties
    VkPhysicalDeviceProperties* props = &args->pProperties->properties;
    props->apiVersion = VK_API_VERSION_1_3;
    props->driverVersion = VK_MAKE_VERSION(1, 0, 0);
    props->vendorID = 0x1AF4;
    props->deviceID = 0x1050;
    props->deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    strncpy(props->deviceName, "Venus Plus Network GPU",
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    generate_venus_plus_uuid(props->pipelineCacheUUID);

    // Find and modify VkPhysicalDeviceDriverProperties in pNext chain
    VkBaseOutStructure* next = (VkBaseOutStructure*)args->pProperties->pNext;
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES) {
            VkPhysicalDeviceDriverProperties* driver =
                (VkPhysicalDeviceDriverProperties*)next;

            driver->driverID = VK_DRIVER_ID_MESA_VENUS;
            strncpy(driver->driverName, "Venus Plus", VK_MAX_DRIVER_NAME_SIZE);
            strncpy(driver->driverInfo, "Network GPU v1.0.0", VK_MAX_DRIVER_INFO_SIZE);
            driver->conformanceVersion = (VkConformanceVersion){1, 3, 0, 0};
        }
        next = next->pNext;
    }
}
```

### Part 3: UUID Generation

**File**: `server/state/fake_gpu_data.cpp`

```cpp
#include <cstring>

void generate_venus_plus_uuid(uint8_t uuid[VK_UUID_SIZE]) {
    // Generate deterministic UUID for Venus Plus
    // Format: "VENUS-PLUS-1.0.0" + padding

    const char* id = "VENUS-PLUS-v1.0";
    memset(uuid, 0, VK_UUID_SIZE);
    memcpy(uuid, id, strlen(id));

    // Or use a proper UUID v5 with Venus Plus namespace
    // UUID v5 namespace: 6ba7b810-9dad-11d1-80b4-00c04fd430c8 (DNS)
    // Name: "venus-plus.local"
}
```

### Part 4: Update Branding Constants

**File**: `server/state/fake_gpu_data.h`

```cpp
#ifndef FAKE_GPU_DATA_H
#define FAKE_GPU_DATA_H

#include <vulkan/vulkan.h>

// Venus Plus branding constants
#define VENUS_PLUS_VENDOR_ID    0x1AF4
#define VENUS_PLUS_DEVICE_ID    0x1050
#define VENUS_PLUS_DEVICE_NAME  "Venus Plus Network GPU"
#define VENUS_PLUS_DRIVER_NAME  "Venus Plus"
#define VENUS_PLUS_DRIVER_INFO  "Network GPU v1.0.0"

#define VENUS_PLUS_VERSION_MAJOR 1
#define VENUS_PLUS_VERSION_MINOR 0
#define VENUS_PLUS_VERSION_PATCH 0

void generate_venus_plus_uuid(uint8_t uuid[VK_UUID_SIZE]);

#endif // FAKE_GPU_DATA_H
```

---

## Task Breakdown

| # | Task | Files | Est. Time |
|---|------|-------|-----------|
| 1 | Define branding constants | `server/state/fake_gpu_data.h` | 15m |
| 2 | Implement UUID generation | `server/state/fake_gpu_data.cpp` | 30m |
| 3 | Modify vkGetPhysicalDeviceProperties | `server/renderer_decoder.c` | 30m |
| 4 | Modify vkGetPhysicalDeviceProperties2 | `server/renderer_decoder.c` | 45m |
| 5 | Test with vulkaninfo --summary | - | 30m |
| 6 | Verify all properties correct | - | 30m |

**Total estimated time**: 3-4 hours

---

## Expected Output

### vulkaninfo --summary

```
==========
VULKANINFO
==========

Vulkan Instance Version: 1.3.x

Devices:
========
GPU0:
    apiVersion         = 1.3.0
    driverVersion      = 1.0.0
    vendorID           = 0x1af4
    deviceID           = 0x1050
    deviceType         = PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU
    deviceName         = Venus Plus Network GPU
    driverID           = DRIVER_ID_MESA_VENUS
    driverName         = Venus Plus
    driverInfo         = Network GPU v1.0.0
    conformanceVersion = 1.3.0.0
    deviceUUID         = 56454e55-532d-504c-5553-2d76312e3000
    driverUUID         = <generated>
```

### vulkaninfo (detailed)

```
VkPhysicalDeviceProperties:
===========================
    apiVersion     = 1.3.0 (4206592)
    driverVersion  = 1.0.0 (4194304)
    vendorID       = 0x1af4
    deviceID       = 0x1050
    deviceType     = PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU
    deviceName     = Venus Plus Network GPU
    pipelineCacheUUID = 56454e55-532d-504c-5553-2d76312e3000

VkPhysicalDeviceDriverProperties:
=================================
    driverID           = DRIVER_ID_MESA_VENUS
    driverName         = Venus Plus
    driverInfo         = Network GPU v1.0.0
    conformanceVersion = 1.3.0.0
```

---

## Testing Checklist

- [ ] `vulkaninfo --summary` shows Venus Plus branding
- [ ] Device type is `VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU`
- [ ] Device name is "Venus Plus Network GPU"
- [ ] Driver name is "Venus Plus"
- [ ] Vendor ID is 0x1AF4
- [ ] Device ID is 0x1050
- [ ] Pipeline cache UUID is deterministic
- [ ] Device limits are still from real GPU
- [ ] Device features are still from real GPU
- [ ] All existing tests still pass
- [ ] No validation errors

---

## Success Criteria

1. **Clear Identity**: vulkaninfo shows Venus Plus branding
2. **Compatibility**: Applications work with branded properties
3. **Determinism**: Same UUID across runs (for pipeline cache)
4. **Transparency**: Limits/features reflect real GPU capabilities

---

## Notes

- Keep real GPU limits for maximum application compatibility
- Use virtio vendor ID (0x1AF4) as it's recognized for virtual devices
- Pipeline cache UUID should be deterministic for caching to work
- Future: Could add version to driver info dynamically

---

## Future Enhancements

- Add build version to `driverInfo` automatically
- Add server hostname to device name ("Venus Plus @ server.local")
- Support multiple virtual GPU "models" with different branding
- Add Venus Plus-specific device extensions for configuration
