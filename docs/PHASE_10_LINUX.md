# Phase 10 Linux: X11 & Wayland WSI

**Goal:** Replace the headless WSI with real Linux window system integration that works on both major compositors and automatically falls back when GBM/DMA-BUF is unavailable.

**Status:** Planned work (nothing is implemented in `client/wsi` yet).

**Duration:** 10 days (Days 86‑95 in the master schedule).

**Prerequisites:**
- Phase 10 Base (graphics pipeline + frame transfer) is functional.
- The server can stream `VenusFrameHeader` + payload back to the client.
- Phase 09 validation and build/test infrastructure stay green.

**Definition of done:** The Phase 10 test app renders through the Venus ICD while showing a window on both X11 and Wayland. GBM/DMA-BUF paths are exercised when supported, CPU fallbacks are verified with the `VENUS_WSI_FORCE_PATH` override, and `ctest --test-dir build/client/tests` still passes.

---

## Scope & Deliverables

- Implement Linux-only versions of the WSI instance/device entrypoints (`vkCreateXcbSurfaceKHR`, `vkCreateXlibSurfaceKHR`, `vkCreateWaylandSurfaceKHR`, `vkGetPhysicalDeviceSurfaceSupportKHR`, `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, `vkGetPhysicalDeviceSurfaceFormatsKHR`, `vkGetPhysicalDeviceSurfacePresentModesKHR`).
- Replace `HeadlessWSI` with a Linux-aware factory in `client/wsi/platform_wsi.h` that chooses between X11/Wayland backends at runtime.
- Provide two execution paths for each compositor:
  - **Primary:** GBM + DMA-BUF (XCB/DRI3 + Present, Wayland `zwp_linux_dmabuf_v1`).
  - **Fallback:** CPU buffers (XPutImage/XShm for X11, `wl_shm` for Wayland).
- Extend swapchain state to store surface metadata (connection, window, wl_surface) so the WSI backend can present frames.
- Update CMake and documentation so the new code builds on Ubuntu/Fedora with the required dev packages.
- Add manual/integration tests to `test-app/phase10` so we can see the rotating triangle and inspect FPS/logs for each path.


## File Map

| Area | Files | Notes |
|------|-------|-------|
| WSI interface | `client/wsi/platform_wsi.h`, new `client/wsi/linux/linux_wsi.{h,cpp}` plus per-path helpers under `client/wsi/linux/` | Introduce `LinuxWSI` derived from `PlatformWSI` and sub-classes for X11/Wayland backends. |
| ICD entrypoints | `client/icd/icd_entrypoints.cpp`, potential helpers under `client/icd/linux_surface.{h,cpp}` | Implement the Linux surface creation functions and surface queries. |
| Swapchain state | `client/state/swapchain_state.{h,cpp}` | Track the selected `PlatformWSI` per swapchain and the local platform surface handles required by the backend. |
| Build files | Root `CMakeLists.txt`, `client/CMakeLists.txt` | Link against `xcb`, `xcb-present`, `xcb-dri3`, `wayland-client`, `xkbcommon`, `drm`, `gbm`. Also include generated Wayland protocol stubs. |
| Tests/docs | `test-app/phase10/`, `docs/BUILD_AND_RUN.md`, this document | Describe how to run each compositor path and how to force fallbacks for validation. |


## Architecture Overview

```
Server GPU ──► Venus frame header + payload ──► client/icd
                               │
                               ▼
                     PlatformWSI::handle_frame()
                               │
                ┌──────────────┴──────────────┐
                │                             │
         X11 backend                   Wayland backend
    (DRI3 + Present /                 (DMA-BUF / wl_shm)
      XPutImage)
```

`PlatformWSI::init()` receives the swapchain create info so it can size buffers. `handle_frame()` receives a `VenusFrameHeader` describing the frame width/height/stride and the compressed payload block. The Linux implementation must decompress if needed, copy into the local buffer, and submit the buffer to the compositor. `shutdown()` tears down connections, destroys GBM BOs, and frees CPU buffers.


### Path Matrix

| Path | Display server | Zero-copy | Libraries | Notes |
|------|----------------|-----------|-----------|-------|
| `X11GbmPath` | X11/XCB + DRI3 + Present | ✅ | `xcb`, `xcb-dri3`, `xcb-present`, `gbm`, `drm` | Import DMA-BUF FD via `xcb_dri3_pixmap_from_buffer` and present it via the Present extension. |
| `WaylandGbmPath` | Wayland + `zwp_linux_dmabuf_v1` | ✅ | `wayland-client`, generated `linux-dmabuf`, `gbm`, `drm` | Creates `wl_buffer` objects backed by GBM BOs and tracks release callbacks. |
| `X11CpuPath` | X11/XCB | ❌ | `xcb`, `xcb-image` | Allocates CPU RGBA buffers and uploads via `xcb_image_put`. |
| `WaylandShmPath` | Wayland | ❌ | `wayland-client`, `wl_shm` | Uses shared memory pools + `wl_buffer` objects. |

Paths are chosen automatically based on the compositor (detected via `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, or `DISPLAY`) and the presence of a usable render node under `/dev/dri/renderD*`. By default the ICD tries GBM/DMA-BUF and transparently falls back to CPU copies if the primary path fails. An environment override (`VENUS_WSI_FORCE_PATH`) forces a specific backend for testing (see below).

`VENUS_WSI_FORCE_PATH` accepted values:
- `x11-cpu` / `xcb-cpu` – force the CPU copy path on X11
- `x11-gbm` – request the DRI3/GBM path (falls back to CPU until implemented)
- `wayland-shm` – force wl_shm buffers
- `wayland-dmabuf` / `wayland-gbm` – request the DMA-BUF path (falls back to wl_shm until implemented)
- `headless` / `none` – skip Linux WSI entirely and fall back to the Phase 10 Base headless writer


### Display Detection Flow

```cpp
std::shared_ptr<PlatformWSI> create_platform_wsi(VkSurfaceKHR surface) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (!is_linux_surface(surface)) {
        return nullptr;  // fall back to headless writer
    }
    LinuxSurface* native = get_linux_surface(surface);
    std::string force = getenv("VENUS_WSI_FORCE_PATH") ?: "";
    if (force == "headless" || force == "none") {
        return nullptr;
    }
    LinuxWSI::BackendKind backend = choose_backend(*native, force);
    return std::make_shared<LinuxWSI>(*native, backend);
#else
    (void)surface;
    return std::make_shared<HeadlessWSI>();
#endif
}
```

### Frame Handling

```cpp
void LinuxWSI::handle_frame(const VenusFrameHeader& frame, const uint8_t* payload) {
    if (!payload || frame.width != width_ || frame.height != height_) {
        return;
    }

    std::span<const uint8_t> decoded = decompress(frame, payload, scratch_);
    BackendBuffer& buffer = acquire_buffer(frame.image_index);
    copy_into_backend(buffer, decoded, frame.stride);
    submit(buffer);
}

std::span<const uint8_t> LinuxWSI::decompress(const VenusFrameHeader& frame,
                                              const uint8_t* payload,
                                              std::vector<uint8_t>& scratch) {
    if (frame.compression == FrameCompressionType::NONE) {
        return {payload, frame.payload_size};
    }
    scratch.clear();
    if (!decompress_rle(frame, payload, scratch)) {
        VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to decode frame";
        return {};
    }
    return scratch;
}
```

Every backend shares the same decode path but has its own `BackendBuffer` implementation:

- **GBM buffers** keep the `gbm_bo*`, `dmabuf fd`, and `map_data` handle returned by `gbm_bo_map`. When a frame arrives we `memcpy` into the mapped pointer and queue the buffer to the compositor. Release callbacks re-map the BO for reuse.
- **CPU buffers** allocate a `malloc`/`mmap` block per swapchain image; frames copy directly into this memory before being uploaded with XPutImage or `wl_surface_attach`.


## Surface & Swapchain Plumbing

### Surface Handles

Add a lightweight `LinuxSurface` structure (stored in `client/state/instance_state` or a new `client/wsi/linux_surface.h`) that captures the native handle from the creation struct. Example:

```cpp
struct LinuxSurface {
    enum class Type { Xcb, Xlib, Wayland } type;
    union {
        struct { xcb_connection_t* connection; xcb_window_t window; } xcb;
        struct { Display* dpy; ::Window window; } xlib;
        struct { wl_display* display; wl_surface* surface; } wayland;
    } native;
};
```

- `vkCreateXcbSurfaceKHR`/`vkCreateXlibSurfaceKHR`/`vkCreateWaylandSurfaceKHR` allocate this structure, stash it behind the `VkSurfaceKHR`, and return it to the application.
- `vkDestroySurfaceKHR` frees the allocation.
- `vkGetPhysicalDeviceXcbPresentationSupportKHR` etc. return `VK_TRUE` if Phase 10 Linux is compiled (the real GPU lives on the server; we simply promise that the client side can blit frames).

### Swapchain wiring

1. During `vkCreateSwapchainKHR` the ICD already calls into `g_swapchain_state.add_swapchain`. Extend that path to:
   - Look up the `LinuxSurface` from `create_info.surface`.
   - Instantiate the correct `PlatformWSI` via `create_platform_wsi(surface)` (the factory should receive the surface so it knows whether X11 or Wayland is needed).
   - Call `wsi->init(create_info, image_count)` and abort if it fails.
   - Store `wsi` in `SwapchainInfo`.
2. `vkQueuePresentKHR` ends with a Venus protocol `VENUS_PLUS_CMD_PRESENT` round-trip. After the reply arrives, forward the `VenusFrameHeader` and payload to `swapchain_info.wsi->handle_frame(frame, payload)`.
3. When `vkDestroySwapchainKHR` runs, call `wsi->shutdown()` once all frames are drained.


## Primary Path: GBM + DMA-BUF

### X11 (DRI3 + Present)

1. **DRM device discovery**: iterate `/dev/dri/renderD128` ‑ `/dev/dri/renderD191` and open the first node that allows `GBM_BO_USE_LINEAR`. Keep the fd for the lifetime of the swapchain.
2. **GBM resources**: allocate one `gbm_bo` per swapchain image, `gbm_bo_map` them for CPU write, and store the returned pointer + `map_data`.
3. **X11 objects**:
   - Create an XCB connection (`xcb_connect`) or reuse the one passed to the surface.
   - Import each BO as a pixmap via `xcb_dri3_pixmap_from_buffer`.
   - Create Present events for `XCB_PRESENT_EVENT_IDLE_NOTIFY` so we know when a buffer becomes reusable.
4. **Presentation**: `handle_frame()` copies the decoded RGBA data into the mapped BO, then issues an `xcb_present_pixmap` for the matching pixmap. Mark the buffer as `in_use` until the idle event fires and then `gbm_bo_map` it again (Present unmaps the BO during flip).
5. **Resizing**: If `frame.width/height` differs from the swapchain extent, destroy and recreate the GBM buffers (this indicates the swapchain was re-created server-side).

### Wayland (linux-dmabuf + dma fences)

1. **Wayland globals**: bind `wl_compositor`, `wl_shm`, and `zwp_linux_dmabuf_v1`. The existing `wl_display` is provided by the app through `VkWaylandSurfaceCreateInfoKHR`.
2. **GBM buffers**: same allocation strategy as the X11 path.
3. **DMA-BUF import**: For each BO, create `zwp_linux_buffer_params_v1`, add plane 0 (fd, stride, offset, modifier), and call `create_immed`. Hook `wl_buffer_listener.release` to flip `Buffer::in_use` back to false and re-map the BO.
4. **Presentation**: `handle_frame()` writes into the mapped BO, calls `wl_surface_attach/commit`, and flushes the display. The compositor calls the release listener when the buffer can be reused.


## Fallback Paths (CPU)

### X11 + XPutImage (or XShm)

1. Allocate `image_count` CPU buffers sized to `width * height * 4` bytes.
2. Create an `xcb_image_t` per buffer using `xcb_image_create_native` and a matching graphics context.
3. `handle_frame()` copies the decoded frame into the CPU buffer and calls `xcb_image_put` to blit into the target window.
4. This path is used when GBM is unavailable or when `VENUS_WSI_FORCE_PATH=x11-cpu`.

### Wayland + wl_shm

1. For each image allocate an `memfd_create` fd, `ftruncate` to buffer size, `mmap` it, create a `wl_shm_pool`, and finally create a `wl_buffer`.
2. Add a `wl_buffer_listener` so the compositor can signal when the buffer is free.
3. `handle_frame()` copies into the mmapped memory, attaches the buffer to the surface, damages the full surface, and commits.


## Surface Queries & Swapchain Behavior

- **`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`**: Use the cached `LinuxSurface` to query the current window geometry. For Wayland (which lacks synchronous geometry queries) fall back to the swapchain extent requested by the application or a default (e.g., 800×600) if unknown.
- **`vkGetPhysicalDeviceSurfaceFormatsKHR`**: Advertise the subset we can display locally (at minimum `VK_FORMAT_B8G8R8A8_UNORM` and `VK_FORMAT_B8G8R8A8_SRGB`). Intersect with the server-supported formats returned as part of swapchain creation (Phase 10 Base).
- **`vkGetPhysicalDeviceSurfacePresentModesKHR`**: Expose `FIFO` and `MAILBOX`. The backend can always emulate FIFO by blocking on the network reply.
- **`vkAcquireNextImageKHR`/`vkQueuePresentKHR`**: Already implemented in Phase 10 Base; the Linux WSI reuses the swapchain image index delivered inside `VenusFrameHeader` and maps it to the correct buffer slot.


## Build & Runtime Dependencies

### Packages

```bash
# Ubuntu / Debian
sudo apt install libxcb1-dev libxcb-dri3-dev libxcb-present-dev \
     libxcb-image0-dev libx11-xcb-dev libwayland-dev wayland-protocols \
     libxkbcommon-dev libdrm-dev libgbm-dev

# Fedora
sudo dnf install libxcb-devel xcb-util-image-devel libX11-xcb-devel \
     wayland-devel wayland-protocols-devel libdrm-devel mesa-libgbm-devel
```

### CMake

```cmake
if(UNIX AND NOT APPLE)
    find_package(XCB REQUIRED COMPONENTS XCB DRI3 PRESENT IMAGE)
    find_package(X11 REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED wayland-client)
    pkg_check_modules(GBM REQUIRED gbm)
    pkg_check_modules(DRM REQUIRED libdrm)

    target_sources(venus_icd PRIVATE
        client/wsi/linux/linux_wsi.cpp
        client/wsi/linux/x11_gbm_wsi.cpp
        client/wsi/linux/x11_cpu_wsi.cpp
        client/wsi/linux/wayland_gbm_wsi.cpp
        client/wsi/linux/wayland_shm_wsi.cpp)

    target_link_libraries(venus_icd PRIVATE
        ${XCB_LIBRARIES} ${X11_LIBRARIES} ${WAYLAND_LIBRARIES}
        ${GBM_LIBRARIES} ${DRM_LIBRARIES})

    target_include_directories(venus_icd PRIVATE
        ${XCB_INCLUDE_DIRS} ${WAYLAND_INCLUDE_DIRS} ${GBM_INCLUDE_DIRS})
endif()
```

The Wayland protocols (linux-dmabuf, xdg-decoration if needed) can be compiled at configure time using `wayland-scanner`. Keep the generated headers under `client/wsi/linux/protocol/` so they remain hermetic.


## Testing Plan

| Scenario | Command | Expected result |
|----------|---------|-----------------|
| X11 + GBM | `VK_DRIVER_FILES=$PWD/build/client/venus_icd.x86_64.json ./build/test-app/venus-test-app --phase 10` | Window appears, logs show `[WSI] Using X11 DRI3 + GBM` and Present events recycle buffers without leaks. |
| X11 fallback | `VENUS_WSI_FORCE_PATH=x11-cpu ...` | Window appears even if `/dev/dri/renderD*` is inaccessible; FPS drop but frames still arrive. |
| Wayland + DMA-BUF | run under a Wayland session with Wayland env vars set | Backend prints `[WSI] Using Wayland DMA-BUF` and release callbacks fire. |
| Wayland fallback | `VENUS_WSI_FORCE_PATH=wayland-shm ...` | Frames copy through `wl_shm`; striding is correct and no protocol errors occur. |

Automated coverage:
- Add a `gtest` under `client/tests` to exercise the detection matrix using fake env vars (no real display needed).
- Extend `test-app/phase10/phase10_test.cpp` to print the detected backend and optionally save a PNG screenshot when `VENUS_WSI_DUMP=1`.
- Use CI smoke test to load the ICD in headless mode, ensuring `create_platform_wsi(nullptr)` falls back to `HeadlessWSI` when no compositor environment variables are set.
- `test-app --phase 10` now attempts to create a native surface (Wayland if `WAYLAND_DISPLAY` is set, otherwise X11 if `DISPLAY` is set) and presents via the active WSI backend before falling back to the headless swapchain check. Use `VENUS_TESTAPP_FORCE_WSI=headless|xcb|wayland` to override detection when validating specific paths.


## Day-by-Day Breakdown

| Day | Tasks |
|-----|-------|
| 1 | Implement Linux surface structs and the `vkCreate*SurfaceKHR` entrypoints. Add stubs for the query functions returning sane defaults. |
| 2 | Create the Linux WSI factory (`create_platform_wsi`) and integrate it with swapchain creation/destruction. |
| 3 | Implement GBM device discovery, helper RAII wrappers, and shared decompression utilities. |
| 4 | Finish the X11 GBM path: buffer allocation, Present wiring, event loop integration. |
| 5 | Implement Wayland DMA-BUF path including protocol binding and buffer release listeners. |
| 6 | Wire up CPU fallback for X11 (XPutImage) and add the `VENUS_WSI_FORCE_PATH` override. |
| 7 | Implement Wayland `wl_shm` fallback and confirm resizing works. |
| 8 | Fill out the surface query functions using real geometry, add logging, and handle swapchain recreation. |
| 9 | Update build files, add detection unit tests, and document run instructions. |
| 10 | Manual test matrix (X11+GBM, X11 fallback, Wayland+GBM, Wayland fallback), polish logs, and fix leaks identified by `valgrind`/ASan. |


## Troubleshooting Notes

- If `gbm_bo_map` returns `nullptr`, ensure the BO was created with `GBM_BO_USE_LINEAR`. Tiled BOs cannot be CPU-mapped.
- DRI3 requires the Present extension; log a warning when `xcb_present_query_version` fails and fall back immediately.
- Wayland compositors will disconnect the client if the buffer stride/pixel format does not match expectations. Keep the DRM fourcc derived from `VkFormat` (`VK_FORMAT_B8G8R8A8_UNORM` → `DRM_FORMAT_ARGB8888`).
- Honor `frame.stride` when copying from the network buffer; do not assume tightly-packed rows.
- On swapchain destruction make sure every outstanding buffer is released before closing the DRM fd, or Present will keep the fd alive and leak file descriptors.


## Success Criteria

- [ ] `vkCreateXcbSurfaceKHR`, `vkCreateXlibSurfaceKHR`, and `vkCreateWaylandSurfaceKHR` return valid surfaces and the app can create swapchains.
- [ ] `PlatformWSI` automatically selects the correct path and logs which backend is active.
- [ ] GBM/DMA-BUF paths display frames with no CPU copies when `/dev/dri/renderD*` is present.
- [ ] CPU fallback paths work when GBM or dma-buf interfaces are missing.
- [ ] `test-app --phase 10` renders a continuously updating triangle on both X11 and Wayland.
- [ ] No memory/file-descriptor leaks detected when repeatedly creating/destroying swapchains.
- [ ] Documentation (`BUILD_AND_RUN.md`, this file) matches the implementation steps and mentions the force-path/testing instructions.

Once all boxes are checked we can move on to Windows/macOS WSI work or harden the Linux implementation with HDR and color-management extensions.
