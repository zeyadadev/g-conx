# Phase 10: Graphics Rendering & WSI

**Render graphics on remote GPU and present to local display**

## Overview

Phase 10 implements graphics pipeline support and window system integration (WSI) for Venus Plus. This phase is split into multiple documents:

| Document | Content | Duration |
|----------|---------|----------|
| [PHASE_10_BASE.md](PHASE_10_BASE.md) | Core graphics commands, frame transfer protocol | 10 days |
| [PHASE_10_LINUX.md](PHASE_10_LINUX.md) | X11 and Wayland WSI | 10 days |
| [PHASE_10_WINDOWS.md](PHASE_10_WINDOWS.md) | Win32 WSI (future) | TBD |
| [PHASE_10_MACOS.md](PHASE_10_MACOS.md) | macOS/iOS WSI (future) | TBD |

**Total Duration**: 20 days (Days 76-95) for Base + Linux

---

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Vulkan API (draw calls, swapchain)                        │
│         │                                                   │
│         ▼                                                   │
│  ┌─────────────────────────────────────────────────┐       │
│  │            VENUS PLUS CLIENT                     │       │
│  ├─────────────────────────────────────────────────┤       │
│  │                                                 │       │
│  │  Graphics Commands ──► Venus Protocol ──► TCP   │       │
│  │                                                 │       │
│  │  Platform WSI ◄─── Frame Data ◄─── TCP          │       │
│  │       │                                         │       │
│  │       ▼                                         │       │
│  │  [Linux]    [Windows]    [macOS]                │       │
│  │  GBM/DRI3   DXGI         IOSurface              │       │
│  │  wl_shm     GDI          CAMetalLayer           │       │
│  │  XPutImage                                      │       │
│  │                                                 │       │
│  └─────────────────────────────────────────────────┘       │
│         │                                                   │
│         ▼                                                   │
│  Display (X11/Wayland/Win32/macOS)                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘

                         │
                         │ Network
                         ▼

┌─────────────────────────────────────────────────────────────┐
│                    VENUS PLUS SERVER                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Venus Protocol ──► Decoder ──► Real Vulkan GPU             │
│                                                             │
│  Frame Capture ◄── Staging Buffer ◄── GPU Render            │
│       │                                                     │
│       ▼                                                     │
│  Compression (LZ4/ZSTD) ──► Network                        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Order

### 1. Phase 10 Base (Required First)

Implements platform-independent functionality:
- Graphics pipeline commands (render pass, framebuffer, draw)
- Network frame transfer protocol
- Server-side frame capture and compression
- Client-side swapchain state management

### 2. Phase 10 Linux (Primary Platform)

Implements Linux WSI with automatic path selection:
- **Primary**: GBM + DMA-BUF (zero-copy)
  - X11: DRI3/Present
  - Wayland: linux-dmabuf
- **Fallback**: CPU buffers (when GBM unavailable)
  - X11: XPutImage
  - Wayland: wl_shm

### 3. Phase 10 Windows (Future)

- DXGI shared textures (primary)
- GDI BitBlt (fallback)

### 4. Phase 10 macOS (Future)

- IOSurface + CAMetalLayer (primary)
- Core Graphics (fallback)

---

## Key Design Decisions

### 1. Frame Transfer Over Network

Frames are transferred as compressed pixel data:
```
Server GPU → Staging Buffer → Compress → Network → Decompress → Client Buffer → Display
```

### 2. Platform-Specific Buffer Allocation

The client allocates display buffers using platform-specific methods:
- **Linux**: GBM (via local DRM device) or CPU malloc
- **Windows**: D3D11 textures or GDI DIB sections
- **macOS**: IOSurface or Core Graphics bitmaps

Venus Plus does NOT require a local Vulkan driver for WSI - it uses lower-level APIs (GBM, DXGI, IOSurface) for buffer management.

### 3. Automatic Fallback

Each platform has a primary (optimized) and fallback (universal) path:
```cpp
if (init_primary_path()) {
    // Use zero-copy / hardware-accelerated path
} else {
    // Fall back to CPU buffers
}
```

---

## Success Criteria

### Base
- [ ] Graphics commands work (render pass, pipeline, draw)
- [ ] Frames transfer over network
- [ ] Compression reduces bandwidth
- [ ] Offscreen triangle test passes

### Linux
- [ ] X11 surfaces work (XCB and Xlib)
- [ ] Wayland surfaces work
- [ ] GBM path auto-detected
- [ ] Fallback works without GBM
- [ ] 30+ FPS on LAN

### Overall
- [ ] All regression tests pass
- [ ] No validation errors
- [ ] PROJECT COMPLETE!

---

## Quick Links

- **[Phase 10 Base](PHASE_10_BASE.md)** - Start here
- **[Phase 10 Linux](PHASE_10_LINUX.md)** - Linux WSI
- **[Phase 10 Windows](PHASE_10_WINDOWS.md)** - Windows WSI (future)
- **[Phase 10 macOS](PHASE_10_MACOS.md)** - macOS WSI (future)

---

## Congratulations!

After completing Phase 10, Venus Plus will be a fully functional network-based Vulkan ICD supporting:
- Remote compute shaders
- Remote graphics rendering
- Local display via WSI

**Thank you for building Venus Plus!**
