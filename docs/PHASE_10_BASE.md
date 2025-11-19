# Phase 10 Base: Graphics Rendering & Frame Transfer Protocol

**Core graphics pipeline and network frame transfer - platform independent**

## Overview

This document covers the platform-independent parts of Phase 10:
- Offscreen graphics rendering commands
- Network frame transfer protocol
- Swapchain emulation architecture

Platform-specific WSI implementations are in:
- [PHASE_10_LINUX.md](PHASE_10_LINUX.md) - X11 and Wayland
- [PHASE_10_WINDOWS.md](PHASE_10_WINDOWS.md) - Win32
- [PHASE_10_MACOS.md](PHASE_10_MACOS.md) - macOS/iOS

**Duration**: 10 days (Days 76-85)
**Prerequisites**: Phase 09 complete

---

## Part A: Offscreen Graphics Commands

### Commands to Implement

#### Render Pass
- [x] `vkCreateRenderPass`
- [x] `vkCreateRenderPass2`
- [x] `vkDestroyRenderPass`

#### Framebuffer
- [x] `vkCreateFramebuffer`
- [x] `vkDestroyFramebuffer`

#### Image View
- [ ] `vkCreateImageView`
- [ ] `vkDestroyImageView`

#### Graphics Pipeline
- [x] `vkCreateGraphicsPipelines`
- [x] Vertex input state
- [x] Input assembly state
- [x] Viewport state
- [x] Rasterization state
- [x] Multisample state
- [ ] Depth stencil state
- [x] Color blend state

#### Drawing Commands
- [x] `vkCmdBeginRenderPass`
- [ ] `vkCmdNextSubpass`
- [x] `vkCmdEndRenderPass`
- [x] `vkCmdBindVertexBuffers`
- [ ] `vkCmdBindIndexBuffer`
- [x] `vkCmdSetViewport`
- [x] `vkCmdSetScissor`
- [x] `vkCmdDraw`
- [ ] `vkCmdDrawIndexed`
- [ ] `vkCmdDrawIndirect`

---

## Part B: Protocol Extensions

### New Wire Commands

The Venus protocol needs extensions for WSI operations. These are Venus Plus-specific additions.

#### Frame Transfer Command *(implemented ✔️ for headless pipeline)*

```cpp
// Command: VENUS_PLUS_CMD_TRANSFER_FRAME
// Direction: Server → Client

// Request (Client → Server)
struct VenusFrameRequest {
    uint32_t swapchain_id;
    uint32_t image_index;
};

// Response (Server → Client)
struct VenusFrameHeader {
    uint32_t magic;              // 0x56504652 ("VPFR")
    uint32_t swapchain_id;
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    uint32_t format;             // VkFormat
    uint32_t compression;        // CompressionType
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t stride;             // Row stride in bytes
    // Followed by pixel data
};

enum CompressionType {
    COMPRESSION_NONE = 0,
    COMPRESSION_LZ4 = 1,
    COMPRESSION_ZSTD = 2,
};
```

#### Swapchain Management Commands *(implemented ✔️)*

```cpp
// VENUS_PLUS_CMD_CREATE_SWAPCHAIN
struct VenusSwapchainCreateInfo {
    uint32_t swapchain_id;       // Client-allocated ID
    uint32_t width;
    uint32_t height;
    uint32_t format;             // VkFormat
    uint32_t image_count;
    uint32_t usage;              // VkImageUsageFlags
    uint32_t present_mode;       // VkPresentModeKHR
};

struct VenusSwapchainCreateReply {
    VkResult result;
    uint32_t actual_image_count;
    // Server allocates real VkImages internally
};

// VENUS_PLUS_CMD_DESTROY_SWAPCHAIN
struct VenusSwapchainDestroy {
    uint32_t swapchain_id;
};

// VENUS_PLUS_CMD_ACQUIRE_IMAGE
struct VenusAcquireImage {
    uint32_t swapchain_id;
    uint64_t timeout;
};

struct VenusAcquireImageReply {
    VkResult result;
    uint32_t image_index;
};

// VENUS_PLUS_CMD_PRESENT
struct VenusPresent {
    uint32_t swapchain_id;
    uint32_t image_index;
    // Triggers frame transfer back to client
};
```

---

## Part C: Server-Side Frame Capture

### Architecture

```
┌─────────────────────────────────────────────┐
│              SERVER                         │
├─────────────────────────────────────────────┤
│                                             │
│  ┌─────────────┐    ┌─────────────┐        │
│  │ Swapchain   │    │ Staging     │        │
│  │ Images      │───►│ Buffers     │        │
│  │ (GPU)       │copy│ (CPU-visible)│        │
│  └─────────────┘    └──────┬──────┘        │
│                            │                │
│                     ┌──────▼──────┐        │
│                     │ Compressor  │        │
│                     │ (LZ4/ZSTD)  │        │
│                     └──────┬──────┘        │
│                            │                │
│                     ┌──────▼──────┐        │
│                     │ Network     │        │
│                     │ Send        │        │
│                     └─────────────┘        │
│                                             │
└─────────────────────────────────────────────┘
```

### Implementation

```cpp
class ServerSwapchain {
    uint32_t id;
    uint32_t width, height;
    VkFormat format;

    // Real GPU images
    std::vector<VkImage> images;
    std::vector<VkDeviceMemory> image_memory;

    // Staging buffers for transfer
    std::vector<VkBuffer> staging_buffers;
    std::vector<VkDeviceMemory> staging_memory;
    std::vector<void*> mapped_staging;

    // Compression buffer
    std::vector<uint8_t> compress_buffer;

public:
    void create(const VenusSwapchainCreateInfo& info) {
        id = info.swapchain_id;
        width = info.width;
        height = info.height;
        format = info.format;

        size_t image_size = width * height * get_format_size(format);

        for (uint32_t i = 0; i < info.image_count; i++) {
            // Create render target image
            VkImageCreateInfo img_info = {
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = {width, height, 1},
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            };
            vkCreateImage(device, &img_info, nullptr, &images[i]);
            // Allocate DEVICE_LOCAL memory...

            // Create staging buffer
            VkBufferCreateInfo buf_info = {
                .size = image_size,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
            };
            vkCreateBuffer(device, &buf_info, nullptr, &staging_buffers[i]);
            // Allocate HOST_VISIBLE | HOST_COHERENT memory...

            // Persistent map
            vkMapMemory(device, staging_memory[i], 0, image_size,
                        0, &mapped_staging[i]);
        }

        // Compression buffer
        compress_buffer.resize(LZ4_compressBound(image_size));
    }

    void transfer_frame(uint32_t image_index, NetworkConnection& conn) {
        VkImage image = images[image_index];
        VkBuffer staging = staging_buffers[image_index];
        void* data = mapped_staging[image_index];
        size_t size = width * height * get_format_size(format);

        // 1. Copy image to staging buffer
        VkCommandBuffer cmd = begin_transfer_cmd();

        // Transition image layout
        VkImageMemoryBarrier barrier = {
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = image
        };
        vkCmdPipelineBarrier(cmd, ...);

        // Copy
        VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}
        };
        vkCmdCopyImageToBuffer(cmd, image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &region);

        // Transition back
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cmd, ...);

        end_and_submit_transfer_cmd(cmd);
        wait_for_transfer();

        // 2. Compress
        size_t compressed_size = LZ4_compress_default(
            (const char*)data,
            (char*)compress_buffer.data(),
            size,
            compress_buffer.size()
        );

        // Use compression only if it actually helps
        bool use_compression = compressed_size < size * 0.9;

        // 3. Send header
        VenusFrameHeader header = {
            .magic = 0x56504652,
            .swapchain_id = id,
            .image_index = image_index,
            .width = width,
            .height = height,
            .format = (uint32_t)format,
            .compression = use_compression ? COMPRESSION_LZ4 : COMPRESSION_NONE,
            .compressed_size = use_compression ? (uint32_t)compressed_size : (uint32_t)size,
            .uncompressed_size = (uint32_t)size,
            .stride = width * get_format_size(format)
        };

        conn.send(&header, sizeof(header));

        // 4. Send data
        if (use_compression) {
            conn.send(compress_buffer.data(), compressed_size);
        } else {
            conn.send(data, size);
        }
    }
};
```

*Current implementation:* For headless validation we copy GPU content lazily and feed it through a lightweight run-length encoding (RLE) before returning `VENUS_PLUS_CMD_PRESENT`. This keeps payloads reasonably small without pulling in a full LZ4/ZSTD dependency yet.

---

## Part D: Client-Side Swapchain State

### Architecture

The client maintains swapchain state and coordinates with platform-specific WSI:

```cpp
// Forward declaration - implemented per-platform
class PlatformWSI;

struct ClientSwapchain {
    uint32_t id;
    VkSwapchainKHR handle;

    // Configuration
    uint32_t width;
    uint32_t height;
    VkFormat format;
    VkPresentModeKHR present_mode;

    // Image management
    uint32_t image_count;
    std::vector<VkImage> images;  // Fake handles for application
    std::vector<bool> image_acquired;

    // Receive buffers (managed by platform WSI)
    std::unique_ptr<PlatformWSI> platform_wsi;

    // Decompression
    std::vector<uint8_t> decompress_buffer;
};
```

### Common Client Operations

```cpp
VkResult vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    auto* swapchain = new ClientSwapchain();
    swapchain->id = allocate_swapchain_id();
    swapchain->width = pCreateInfo->imageExtent.width;
    swapchain->height = pCreateInfo->imageExtent.height;
    swapchain->format = pCreateInfo->imageFormat;
    swapchain->present_mode = pCreateInfo->presentMode;

    // 1. Tell server to create swapchain
    VenusSwapchainCreateInfo create_info = {
        .swapchain_id = swapchain->id,
        .width = swapchain->width,
        .height = swapchain->height,
        .format = (uint32_t)swapchain->format,
        .image_count = pCreateInfo->minImageCount,
        .usage = pCreateInfo->imageUsage,
        .present_mode = (uint32_t)swapchain->present_mode
    };

    send_command(VENUS_PLUS_CMD_CREATE_SWAPCHAIN, &create_info);

    VenusSwapchainCreateReply reply;
    receive_reply(&reply);

    if (reply.result != VK_SUCCESS) {
        delete swapchain;
        return reply.result;
    }

    swapchain->image_count = reply.actual_image_count;

    // 2. Create fake image handles for application
    for (uint32_t i = 0; i < swapchain->image_count; i++) {
        swapchain->images.push_back(allocate_handle<VkImage>());
        swapchain->image_acquired.push_back(false);
    }

    // 3. Initialize platform-specific WSI
    swapchain->platform_wsi = create_platform_wsi(pCreateInfo->surface);
    swapchain->platform_wsi->init_buffers(
        swapchain->width,
        swapchain->height,
        swapchain->format,
        swapchain->image_count
    );

    // 4. Allocate decompression buffer
    size_t frame_size = swapchain->width * swapchain->height *
                        get_format_size(swapchain->format);
    swapchain->decompress_buffer.resize(frame_size);

    *pSwapchain = (VkSwapchainKHR)swapchain;
    return VK_SUCCESS;
}

VkResult vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchainHandle,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex) {

    auto* swapchain = (ClientSwapchain*)swapchainHandle;

    // Request available image from server
    // Server tracks which images are still being rendered
    VenusAcquireImage request = {
        .swapchain_id = swapchain->id,
        .timeout = timeout
    };
    send_command(VENUS_PLUS_CMD_ACQUIRE_IMAGE, &request);

    VenusAcquireImageReply reply;
    receive_reply(&reply);

    if (reply.result == VK_TIMEOUT || reply.result == VK_NOT_READY) {
        return reply.result;
    }

    if (reply.result != VK_SUCCESS) {
        return reply.result;
    }

    *pImageIndex = reply.image_index;
    swapchain->image_acquired[reply.image_index] = true;

    // Signal sync objects AFTER server confirms image is available
    if (semaphore) signal_semaphore(semaphore);
    if (fence) signal_fence(fence);

    return VK_SUCCESS;
}

// Also support vkAcquireNextImage2KHR
VkResult vkAcquireNextImage2KHR(
    VkDevice device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {

    return vkAcquireNextImageKHR(
        device,
        pAcquireInfo->swapchain,
        pAcquireInfo->timeout,
        pAcquireInfo->semaphore,
        pAcquireInfo->fence,
        pImageIndex
    );
}

VkResult vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo) {

    // Wait for render semaphores
    for (uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; i++) {
        wait_semaphore(pPresentInfo->pWaitSemaphores[i]);
    }

    VkResult result = VK_SUCCESS;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        auto* swapchain = (ClientSwapchain*)pPresentInfo->pSwapchains[i];
        uint32_t image_index = pPresentInfo->pImageIndices[i];

        // 1. Request frame from server
        VenusPresent present_cmd = {
            .swapchain_id = swapchain->id,
            .image_index = image_index
        };
        send_command(VENUS_PLUS_CMD_PRESENT, &present_cmd);

        // 2. Receive frame header
        VenusFrameHeader header;
        receive_data(&header, sizeof(header));

        if (header.magic != 0x56504652) {
            result = VK_ERROR_DEVICE_LOST;
            continue;
        }

        // 3. Receive and decompress frame data
        // Get buffer info including stride
        auto buf_info = swapchain->platform_wsi->get_buffer(image_index);

        uint32_t src_stride = header.stride;
        uint32_t dst_stride = buf_info.stride;
        uint32_t row_bytes = header.width * get_format_size((VkFormat)header.format);

        if (header.compression == COMPRESSION_NONE) {
            if (src_stride == dst_stride) {
                // Fast path: strides match
                receive_data(buf_info.data, header.uncompressed_size);
            } else {
                // Slow path: receive to temp, copy line by line
                receive_data(swapchain->decompress_buffer.data(), header.uncompressed_size);

                uint8_t* src = swapchain->decompress_buffer.data();
                uint8_t* dst = (uint8_t*)buf_info.data;
                for (uint32_t y = 0; y < header.height; y++) {
                    memcpy(dst, src, row_bytes);
                    src += src_stride;
                    dst += dst_stride;
                }
            }
        } else {
            // Receive compressed data
            std::vector<uint8_t> compressed(header.compressed_size);
            receive_data(compressed.data(), header.compressed_size);

            // Decompress to temp buffer first
            LZ4_decompress_safe(
                (const char*)compressed.data(),
                (char*)swapchain->decompress_buffer.data(),
                header.compressed_size,
                header.uncompressed_size
            );

            // Copy to destination respecting stride
            if (src_stride == dst_stride) {
                memcpy(buf_info.data, swapchain->decompress_buffer.data(),
                       header.uncompressed_size);
            } else {
                uint8_t* src = swapchain->decompress_buffer.data();
                uint8_t* dst = (uint8_t*)buf_info.data;
                for (uint32_t y = 0; y < header.height; y++) {
                    memcpy(dst, src, row_bytes);
                    src += src_stride;
                    dst += dst_stride;
                }
            }
        }

        // 4. Notify buffer access complete (for unmap/sync)
        swapchain->platform_wsi->end_buffer_access(image_index);

        // 5. Present to display (platform-specific)
        swapchain->platform_wsi->present(image_index);

        // 6. Mark image as available
        swapchain->image_acquired[image_index] = false;

        if (pPresentInfo->pResults) {
            pPresentInfo->pResults[i] = VK_SUCCESS;
        }
    }

    return result;
}

void vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchainHandle,
    const VkAllocationCallbacks* pAllocator) {

    if (!swapchainHandle) return;

    auto* swapchain = (ClientSwapchain*)swapchainHandle;

    // 1. Wait for all pending operations to complete
    // This ensures no images are in flight
    vkDeviceWaitIdle(device);

    // 2. Tell server to destroy swapchain
    VenusSwapchainDestroy destroy_cmd = {
        .swapchain_id = swapchain->id
    };
    send_command(VENUS_PLUS_CMD_DESTROY_SWAPCHAIN, &destroy_cmd);

    // Wait for server confirmation
    VkResult result;
    receive_reply(&result);

    // 3. Destroy platform WSI resources (GBM buffers, wl_buffers, etc.)
    if (swapchain->platform_wsi) {
        swapchain->platform_wsi->destroy();
    }

    // 4. Free client-side state
    swapchain->images.clear();
    swapchain->image_acquired.clear();
    swapchain->decompress_buffer.clear();

    delete swapchain;
}

VkResult vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchainHandle,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages) {

    auto* swapchain = (ClientSwapchain*)swapchainHandle;

    if (!pSwapchainImages) {
        *pSwapchainImageCount = swapchain->image_count;
        return VK_SUCCESS;
    }

    uint32_t count = std::min(*pSwapchainImageCount, swapchain->image_count);
    for (uint32_t i = 0; i < count; i++) {
        pSwapchainImages[i] = swapchain->images[i];
    }
    *pSwapchainImageCount = count;

    return (count < swapchain->image_count) ? VK_INCOMPLETE : VK_SUCCESS;
}
```

---

## Part E: Platform WSI Interface *(headless stub ✔️)*

A minimal headless implementation now writes the received frames to `swapchain_<id>_image_<n>.rgba` for inspection. Future work will swap in real window-system backends per platform.

### Abstract Interface

```cpp
class PlatformWSI {
public:
    virtual ~PlatformWSI() = default;

    virtual bool init(const VkSwapchainCreateInfoKHR& info,
                      uint32_t image_count) = 0;
    virtual void handle_frame(const VenusFrameHeader& frame,
                              const uint8_t* data) = 0;
    virtual void shutdown() = 0;
};

std::shared_ptr<PlatformWSI> create_platform_wsi(VkSurfaceKHR surface);
```

---

## Task Breakdown (Base)

| Day | Tasks |
|-----|-------|
| 1-2 | Render pass, image view, framebuffer commands |
| 3-5 | Graphics pipeline creation |
| 6-7 | Drawing commands |
| 8 | Protocol extensions (wire structs, server handlers) |
| 9 | Server frame capture and compression |
| 10 | Client swapchain state, testing |

---

## Test Application

### Offscreen Triangle Test

**File**: `test-app/phase10/phase10_triangle.cpp`

Renders a triangle to an offscreen image and saves to file. Validates graphics pipeline without WSI.

```cpp
int main() {
    // Setup
    VkInstance instance = create_instance();
    VkDevice device = create_device(instance);

    // Create offscreen image
    VkImage color_image = create_image(800, 600, VK_FORMAT_R8G8B8A8_UNORM,
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkImageView color_view = create_image_view(color_image);

    // Create render pass
    VkRenderPass render_pass = create_render_pass(VK_FORMAT_R8G8B8A8_UNORM);

    // Create framebuffer
    VkFramebuffer framebuffer = create_framebuffer(render_pass, color_view, 800, 600);

    // Create pipeline
    VkPipeline pipeline = create_triangle_pipeline(render_pass);

    // Record commands
    VkCommandBuffer cmd = begin_commands();

    VkRenderPassBeginInfo begin_info = {
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {800, 600}},
        .clearValueCount = 1,
        .pClearValues = &clear_color  // black
    };

    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    end_and_submit(cmd);
    wait_idle();

    // Read back and save
    save_image_to_file(color_image, "triangle.png");

    printf("Phase 10 Base: Triangle rendered to triangle.png\n");
    return 0;
}
```

---

## Success Criteria

- [ ] All graphics commands implemented
- [x] Render pass/framebuffer creation works
- [x] Graphics pipeline creation works
- [x] Drawing commands execute on server
- [x] Frame transfer protocol working (headless swapchain round-trip)
- [x] Compression reduces bandwidth (simple RLE encoding)
- [x] Triangle test renders correctly (saves `triangle.png`)
- [ ] No validation errors

---

## Next Steps

After completing Phase 10 Base, implement platform-specific WSI:

1. **[PHASE_10_LINUX.md](PHASE_10_LINUX.md)** - X11 and Wayland support
2. **[PHASE_10_WINDOWS.md](PHASE_10_WINDOWS.md)** - Win32 support
3. **[PHASE_10_MACOS.md](PHASE_10_MACOS.md)** - macOS support
