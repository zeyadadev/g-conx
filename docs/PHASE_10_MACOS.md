# Phase 10 macOS: Metal/CAMetalLayer WSI

**macOS and iOS-specific WSI implementation**

## Overview

This document covers macOS/iOS WSI implementation for Venus Plus.

**Status**: Future implementation
**Prerequisites**: Phase 10 Base complete

---

## Architecture

### Approach: IOSurface + CAMetalLayer

macOS uses IOSurface for zero-copy buffer sharing between processes and with the compositor:

```objc
@interface VenusMacOSWSI : NSObject <PlatformWSI>
{
    CAMetalLayer* metalLayer;
    id<MTLDevice> device;

    NSMutableArray<IOSurfaceRef>* surfaces;
    NSMutableArray<id<MTLTexture>>* textures;
}
@end
```

**Key Components**:
- **IOSurface**: Cross-process shareable GPU buffers
- **CAMetalLayer**: Direct display without copying
- **MTLTexture**: Metal texture wrapping IOSurface

---

## Implementation Sketch

### Surface Creation

```cpp
// C++ wrapper
VkResult vkCreateMetalSurfaceEXT(
    VkInstance instance,
    const VkMetalSurfaceCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    auto* surface = new VenusSurface();
    surface->type = SURFACE_TYPE_METAL;
    surface->metal.layer = pCreateInfo->pLayer;

    *pSurface = (VkSurfaceKHR)surface;
    return VK_SUCCESS;
}

// Also support macOS-specific
VkResult vkCreateMacOSSurfaceMVK(
    VkInstance instance,
    const VkMacOSSurfaceCreateInfoMVK* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface);
```

### IOSurface Buffer Management

```objc
@implementation VenusMacOSWSI

- (BOOL)initBuffersWithWidth:(uint32_t)w
                      height:(uint32_t)h
                      format:(VkFormat)format
                       count:(uint32_t)count {

    // Get Metal device
    device = MTLCreateSystemDefaultDevice();
    if (!device) return NO;

    surfaces = [NSMutableArray arrayWithCapacity:count];
    textures = [NSMutableArray arrayWithCapacity:count];

    for (uint32_t i = 0; i < count; i++) {
        // Create IOSurface
        NSDictionary* props = @{
            (id)kIOSurfaceWidth: @(w),
            (id)kIOSurfaceHeight: @(h),
            (id)kIOSurfaceBytesPerElement: @(4),
            (id)kIOSurfacePixelFormat: @(kCVPixelFormatType_32BGRA)
        };

        IOSurfaceRef surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!surface) return NO;

        [surfaces addObject:(__bridge id)surface];

        // Create Metal texture from IOSurface
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:w height:h mipmapped:NO];

        id<MTLTexture> texture = [device
            newTextureWithDescriptor:desc
            iosurface:surface
            plane:0];

        [textures addObject:texture];
    }

    // Configure CAMetalLayer
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = NO;

    return YES;
}

- (void*)getBuffer:(uint32_t)index {
    IOSurfaceRef surface = (__bridge IOSurfaceRef)surfaces[index];

    // Lock for CPU access
    IOSurfaceLock(surface, 0, NULL);

    return IOSurfaceGetBaseAddress(surface);
}

- (void)present:(uint32_t)index {
    IOSurfaceRef surface = (__bridge IOSurfaceRef)surfaces[index];

    // Unlock after CPU write
    IOSurfaceUnlock(surface, 0, NULL);

    // Get drawable and copy
    id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
    if (!drawable) return;

    id<MTLCommandBuffer> cmdBuffer = [commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmdBuffer blitCommandEncoder];

    [blit copyFromTexture:textures[index]
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                toTexture:drawable.texture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];

    [blit endEncoding];
    [cmdBuffer presentDrawable:drawable];
    [cmdBuffer commit];
}

- (void)destroy {
    for (id surface in surfaces) {
        CFRelease((__bridge CFTypeRef)surface);
    }
    [surfaces removeAllObjects];
    [textures removeAllObjects];
}

@end
```

### CPU Fallback (Core Graphics)

For systems without Metal (older macOS):

```objc
@implementation VenusCoreGraphicsWSI

- (void*)getBuffer:(uint32_t)index {
    return buffers[index].data;
}

- (void)present:(uint32_t)index {
    // Create CGImage from buffer
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        buffers[index].data,
        width, height,
        8, width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little
    );

    CGImageRef image = CGBitmapContextCreateImage(context);

    // Draw to layer
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.contents = (__bridge id)image;
    [CATransaction commit];

    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
}

@end
```

---

## Task Breakdown

| Task | Description |
|------|-------------|
| 1 | Surface creation (Metal, macOS) |
| 2 | Surface capability queries |
| 3 | IOSurface + CAMetalLayer path |
| 4 | Core Graphics fallback |
| 5 | Testing |

---

## Build Requirements

```cmake
if(APPLE)
    target_sources(venus_icd PRIVATE
        client/wsi/macos_wsi.mm
        client/wsi/macos_metal.mm
        client/wsi/macos_cg.mm
    )

    target_link_libraries(venus_icd PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework IOSurface"
        "-framework Cocoa"
    )

    # iOS
    if(IOS)
        target_link_libraries(venus_icd PRIVATE
            "-framework UIKit"
        )
    endif()
endif()
```

---

## iOS Considerations

- Use `UIView` layer instead of `NSView`
- `CAMetalLayer` works the same
- May need `CADisplayLink` for vsync timing
- Handle orientation changes

---

## Notes

- IOSurface provides zero-copy sharing with WindowServer
- Metal is available on macOS 10.11+ and all iOS devices
- Core Graphics fallback for older systems
- Consider handling Retina/HiDPI scaling

---

## Status

**Not yet implemented.** This is a placeholder for future macOS/iOS support.
