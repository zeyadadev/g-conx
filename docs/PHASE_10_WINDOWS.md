# Phase 10 Windows: Win32 WSI

**Windows-specific WSI implementation**

## Overview

This document covers Windows WSI implementation for Venus Plus.

**Status**: Future implementation
**Prerequisites**: Phase 10 Base complete, Phase 10 Linux complete

---

## Architecture

### Approach Options

#### Option 1: DXGI SharedHandle (Recommended)

Use D3D11 shared textures for zero-copy to DWM compositor:

```cpp
class Win32_DXGI_WSI : public PlatformWSI {
    ID3D11Device* d3d_device;
    IDXGISwapChain* dxgi_swapchain;

    struct Buffer {
        ID3D11Texture2D* texture;
        HANDLE shared_handle;
        void* mapped;
    };
    std::vector<Buffer> buffers;
};
```

**Pros**: Zero-copy to compositor, hardware accelerated
**Cons**: Requires D3D11 device on client

#### Option 2: GDI BitBlt (Fallback)

CPU buffer with GDI blit:

```cpp
class Win32_GDI_WSI : public PlatformWSI {
    HWND hwnd;
    HDC hdc;

    struct Buffer {
        void* data;
        HBITMAP bitmap;
        HDC mem_dc;
    };
    std::vector<Buffer> buffers;

    void present(uint32_t index) {
        BitBlt(hdc, 0, 0, width, height, buffers[index].mem_dc, 0, 0, SRCCOPY);
    }
};
```

**Pros**: Works everywhere
**Cons**: Extra copies, CPU overhead

---

## Implementation Sketch

### Surface Creation

```cpp
VkResult vkCreateWin32SurfaceKHR(
    VkInstance instance,
    const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    auto* surface = new VenusSurface();
    surface->type = SURFACE_TYPE_WIN32;
    surface->win32.hinstance = pCreateInfo->hinstance;
    surface->win32.hwnd = pCreateInfo->hwnd;

    *pSurface = (VkSurfaceKHR)surface;
    return VK_SUCCESS;
}
```

### DXGI Path

```cpp
bool init_buffers(uint32_t w, uint32_t h, VkFormat format, uint32_t count) {
    // Create D3D11 device
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                      D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                      nullptr, 0, D3D11_SDK_VERSION,
                      &d3d_device, nullptr, &d3d_context);

    // Create DXGI swapchain
    DXGI_SWAP_CHAIN_DESC desc = {
        .BufferDesc = {w, h, {60, 1}, DXGI_FORMAT_B8G8R8A8_UNORM},
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = count,
        .OutputWindow = hwnd,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD
    };

    IDXGIFactory* factory;
    CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
    factory->CreateSwapChain(d3d_device, &desc, &dxgi_swapchain);

    // Create staging textures for CPU write
    for (uint32_t i = 0; i < count; i++) {
        D3D11_TEXTURE2D_DESC tex_desc = {
            .Width = w,
            .Height = h,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
        };
        d3d_device->CreateTexture2D(&tex_desc, nullptr, &buffers[i].staging);
    }

    return true;
}

void* get_buffer(uint32_t index) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    d3d_context->Map(buffers[index].staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    return mapped.pData;
}

void present(uint32_t index) {
    d3d_context->Unmap(buffers[index].staging, 0);

    // Copy to backbuffer
    ID3D11Texture2D* backbuffer;
    dxgi_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
    d3d_context->CopyResource(backbuffer, buffers[index].staging);
    backbuffer->Release();

    // Present
    dxgi_swapchain->Present(1, 0);  // VSync
}
```

### GDI Fallback

```cpp
bool init_buffers(uint32_t w, uint32_t h, VkFormat format, uint32_t count) {
    hdc = GetDC(hwnd);

    for (uint32_t i = 0; i < count; i++) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        buffers[i].mem_dc = CreateCompatibleDC(hdc);
        buffers[i].bitmap = CreateDIBSection(
            hdc, &bmi, DIB_RGB_COLORS,
            &buffers[i].data, nullptr, 0
        );
        SelectObject(buffers[i].mem_dc, buffers[i].bitmap);
    }

    return true;
}

void* get_buffer(uint32_t index) {
    return buffers[index].data;
}

void present(uint32_t index) {
    BitBlt(hdc, 0, 0, width, height,
           buffers[index].mem_dc, 0, 0, SRCCOPY);
}
```

---

## Task Breakdown

| Task | Description |
|------|-------------|
| 1 | Surface creation (vkCreateWin32SurfaceKHR) |
| 2 | Surface capability queries |
| 3 | DXGI swapchain path |
| 4 | GDI fallback path |
| 5 | Auto-detection |
| 6 | Testing |

---

## Build Requirements

```cmake
if(WIN32)
    target_sources(venus_icd PRIVATE
        client/wsi/win32_wsi.cpp
        client/wsi/win32_dxgi.cpp
        client/wsi/win32_gdi.cpp
    )

    target_link_libraries(venus_icd PRIVATE
        d3d11
        dxgi
        gdi32
        user32
    )
endif()
```

---

## Notes

- DXGI requires D3D11 device on client (most Windows systems have this)
- GDI fallback works on all Windows versions
- Consider DirectComposition for better compositing on Windows 8+
- May need to handle DPI scaling

---

## Status

**Not yet implemented.** This is a placeholder for future Windows support.
