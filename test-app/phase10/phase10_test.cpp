#include "phase10_test.h"

#include "logging.h"
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

namespace {

constexpr uint32_t kImageWidth = 256;
constexpr uint32_t kImageHeight = 256;

const uint32_t kVertexShaderSpv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000021, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0009000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00000013,
    0x00000018, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
    0x00050005, 0x00000009, 0x4374756f, 0x726f6c6f, 0x00000000, 0x00040005, 0x0000000b, 0x6f436e69,
    0x00726f6c, 0x00060005, 0x00000011, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x00000011, 0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x00000011, 0x00000001,
    0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000, 0x00070006, 0x00000011, 0x00000002, 0x435f6c67,
    0x4470696c, 0x61747369, 0x0065636e, 0x00070006, 0x00000011, 0x00000003, 0x435f6c67, 0x446c6c75,
    0x61747369, 0x0065636e, 0x00030005, 0x00000013, 0x00000000, 0x00050005, 0x00000018, 0x6f506e69,
    0x69746973, 0x00006e6f, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000001, 0x00050048, 0x00000011, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x00000011, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x00000011, 0x00000002, 0x0000000b,
    0x00000003, 0x00050048, 0x00000011, 0x00000003, 0x0000000b, 0x00000004, 0x00030047, 0x00000011,
    0x00000002, 0x00040047, 0x00000018, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000003, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b,
    0x00000001, 0x00040017, 0x0000000d, 0x00000006, 0x00000004, 0x00040015, 0x0000000e, 0x00000020,
    0x00000000, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000001, 0x0004001c, 0x00000010, 0x00000006,
    0x0000000f, 0x0006001e, 0x00000011, 0x0000000d, 0x00000006, 0x00000010, 0x00000010, 0x00040020,
    0x00000012, 0x00000003, 0x00000011, 0x0004003b, 0x00000012, 0x00000013, 0x00000003, 0x00040015,
    0x00000014, 0x00000020, 0x00000001, 0x0004002b, 0x00000014, 0x00000015, 0x00000000, 0x00040017,
    0x00000016, 0x00000006, 0x00000002, 0x00040020, 0x00000017, 0x00000001, 0x00000016, 0x0004003b,
    0x00000017, 0x00000018, 0x00000001, 0x0004002b, 0x00000006, 0x0000001a, 0x00000000, 0x0004002b,
    0x00000006, 0x0000001b, 0x3f800000, 0x00040020, 0x0000001f, 0x00000003, 0x0000000d, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007,
    0x0000000c, 0x0000000b, 0x0003003e, 0x00000009, 0x0000000c, 0x0004003d, 0x00000016, 0x00000019,
    0x00000018, 0x00050051, 0x00000006, 0x0000001c, 0x00000019, 0x00000000, 0x00050051, 0x00000006,
    0x0000001d, 0x00000019, 0x00000001, 0x00070050, 0x0000000d, 0x0000001e, 0x0000001c, 0x0000001d,
    0x0000001a, 0x0000001b, 0x00050041, 0x0000001f, 0x00000020, 0x00000013, 0x00000015, 0x0003003e,
    0x00000020, 0x0000001e, 0x000100fd, 0x00010038,
};

const uint32_t kFragmentShaderSpv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000013, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00050005, 0x00000009, 0x4374756f, 0x726f6c6f, 0x00000000, 0x00040005, 0x0000000c,
    0x6f436e69, 0x00726f6c, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000c,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a,
    0x00000006, 0x00000003, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
    0x0000000c, 0x00000001, 0x0004002b, 0x00000006, 0x0000000e, 0x3f800000, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d,
    0x0000000c, 0x00050051, 0x00000006, 0x0000000f, 0x0000000d, 0x00000000, 0x00050051, 0x00000006,
    0x00000010, 0x0000000d, 0x00000001, 0x00050051, 0x00000006, 0x00000011, 0x0000000d, 0x00000002,
    0x00070050, 0x00000007, 0x00000012, 0x0000000f, 0x00000010, 0x00000011, 0x0000000e, 0x0003003e,
    0x00000009, 0x00000012, 0x000100fd, 0x00010038,
};

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

uint32_t find_memory_type(uint32_t type_bits,
                          VkMemoryPropertyFlags desired,
                          const VkPhysicalDeviceMemoryProperties& props) {
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & desired) == desired) {
            return i;
        }
    }
    return UINT32_MAX;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static bool initialized = false;
    static uint32_t table[256];
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (uint32_t j = 0; j < 8; ++j) {
                if (c & 1) {
                    c = 0xedb88320u ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    const uint32_t mod = 65521u;
    uint32_t a = 1;
    uint32_t b = 0;
    size_t processed = 0;
    while (processed < len) {
        size_t chunk = std::min<size_t>(len - processed, 5552);
        for (size_t i = 0; i < chunk; ++i) {
            a += data[processed + i];
            b += a;
        }
        a %= mod;
        b %= mod;
        processed += chunk;
    }
    return (b << 16) | a;
}

void append_u32_be(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

bool write_png(const std::string& path,
               uint32_t width,
               uint32_t height,
               const std::vector<uint8_t>& rgba) {
    if (rgba.size() != static_cast<size_t>(width) * height * 4u) {
        return false;
    }

    std::vector<uint8_t> filtered;
    filtered.reserve((width * 4u + 1u) * height);
    for (uint32_t y = 0; y < height; ++y) {
        filtered.push_back(0); // filter type 0
        const uint8_t* row = rgba.data() + y * width * 4u;
        filtered.insert(filtered.end(), row, row + width * 4u);
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01); // no compression, fastest

    size_t remaining = filtered.size();
    size_t offset = 0;
    while (remaining > 0) {
        const uint32_t chunk = static_cast<uint32_t>(std::min<size_t>(remaining, 0xffff));
        const bool final_block = remaining <= 0xffff;
        zlib.push_back(static_cast<uint8_t>(final_block ? 1 : 0));
        zlib.push_back(chunk & 0xff);
        zlib.push_back((chunk >> 8) & 0xff);
        uint16_t nlen = static_cast<uint16_t>(~chunk);
        zlib.push_back(nlen & 0xff);
        zlib.push_back((nlen >> 8) & 0xff);
        zlib.insert(zlib.end(), filtered.begin() + offset, filtered.begin() + offset + chunk);
        offset += chunk;
        remaining -= chunk;
    }
    uint32_t adler = adler32(filtered.data(), filtered.size());
    zlib.push_back(static_cast<uint8_t>((adler >> 24) & 0xff));
    zlib.push_back(static_cast<uint8_t>((adler >> 16) & 0xff));
    zlib.push_back(static_cast<uint8_t>((adler >> 8) & 0xff));
    zlib.push_back(static_cast<uint8_t>(adler & 0xff));

    std::vector<uint8_t> png;
    png.reserve(8 + 12 + 13 + 12 + zlib.size() + 12);
    const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), signature, signature + 8);

    auto write_chunk = [&](const char* type, const std::vector<uint8_t>& payload) {
        append_u32_be(png, static_cast<uint32_t>(payload.size()));
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), payload.begin(), payload.end());
        uint32_t crc = crc32(png.data() + png.size() - payload.size() - 4, payload.size() + 4);
        append_u32_be(png, crc);
    };

    std::vector<uint8_t> ihdr;
    ihdr.reserve(13);
    append_u32_be(ihdr, width);
    append_u32_be(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    write_chunk("IHDR", ihdr);

    write_chunk("IDAT", zlib);
    write_chunk("IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
}

bool flush_memory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = 0;
    range.size = size;
    return vkFlushMappedMemoryRanges(device, 1, &range) == VK_SUCCESS;
}

bool invalidate_memory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size) {
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = memory;
    range.offset = 0;
    range.size = size;
    return vkInvalidateMappedMemoryRanges(device, 1, &range) == VK_SUCCESS;
}

struct Vertex {
    float position[2];
    float color[3];
};

const std::array<Vertex, 3> kVertices = {{
    {{0.0f, -0.6f}, {1.0f, 0.0f, 0.0f}},
    {{0.6f, 0.6f}, {0.0f, 1.0f, 0.0f}},
    {{-0.6f, 0.6f}, {0.0f, 0.0f, 1.0f}},
}};

std::set<std::string> list_swapchain_files() {
    namespace fs = std::filesystem;
    std::set<std::string> files;
    for (const auto& entry : fs::directory_iterator(fs::current_path())) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("swapchain_", 0) == 0 && entry.path().extension() == ".rgba") {
            files.insert(entry.path().string());
        }
    }
    return files;
}

bool wait_for_new_frame(const std::set<std::string>& before,
                        std::string* out_path,
                        std::chrono::milliseconds timeout) {
    if (!out_path) {
        return false;
    }
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        std::set<std::string> after = list_swapchain_files();
        for (const auto& path : after) {
            if (before.find(path) == before.end()) {
                *out_path = path;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool validate_frame_file(const std::string& path, uint32_t width, uint32_t height) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::vector<uint8_t> data(std::istreambuf_iterator<char>(file), {});
    if (data.size() != static_cast<size_t>(width) * height * 4u) {
        return false;
    }
    const size_t center = (static_cast<size_t>(height) / 2 * width + (width / 2)) * 4u;
    if (center + 3 >= data.size()) {
        return false;
    }
    const uint8_t r = data[center];
    const uint8_t g = data[center + 1];
    const uint8_t b = data[center + 2];
    return (r + g + b) > 0;
}

} // namespace

bool run_phase10_test() {
    TEST_LOG_INFO() << "Phase 10: Graphics Rendering";

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence render_fence = VK_NULL_HANDLE;
    VkShaderModule vert_shader = VK_NULL_HANDLE;
    VkShaderModule frag_shader = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_handle = VK_NULL_HANDLE;
    BufferResource vertex_buffer;
    ImageResource color_image;
    VkImageView color_view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    BufferResource readback_buffer;

    auto cleanup = [&]() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        if (device != VK_NULL_HANDLE && swapchain_handle != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain_handle, nullptr);
        }
        if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
        if (color_view) vkDestroyImageView(device, color_view, nullptr);
        if (color_image.image) vkDestroyImage(device, color_image.image, nullptr);
        if (color_image.memory) vkFreeMemory(device, color_image.memory, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (vert_shader) vkDestroyShaderModule(device, vert_shader, nullptr);
        if (frag_shader) vkDestroyShaderModule(device, frag_shader, nullptr);
        if (vertex_buffer.buffer) vkDestroyBuffer(device, vertex_buffer.buffer, nullptr);
        if (vertex_buffer.memory) vkFreeMemory(device, vertex_buffer.memory, nullptr);
        if (readback_buffer.buffer) vkDestroyBuffer(device, readback_buffer.buffer, nullptr);
        if (readback_buffer.memory) vkFreeMemory(device, readback_buffer.memory, nullptr);
        if (render_fence) vkDestroyFence(device, render_fence, nullptr);
        if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    };

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase10";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed";
        cleanup();
        return false;
    }
    TEST_LOG_INFO() << "✅ Instance created";

    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
    if (phys_count == 0) {
        TEST_LOG_ERROR() << "✗ No physical devices available";
        cleanup();
        return false;
    }
    std::vector<VkPhysicalDevice> devices(phys_count);
    vkEnumeratePhysicalDevices(instance, &phys_count, devices.data());
    physical_device = devices[0];

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_props.data());
    bool found_queue = false;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family = i;
            found_queue = true;
            break;
        }
    }
    if (!found_queue) {
        TEST_LOG_ERROR() << "✗ No graphics queue family found";
        cleanup();
        return false;
    }
    TEST_LOG_INFO() << "✅ Selected graphics queue family " << queue_family;

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char* device_extensions[] = { "VK_KHR_swapchain" };

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;

    if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed";
        cleanup();
        return false;
    }
    vkGetDeviceQueue(device, queue_family, 0, &queue);
    TEST_LOG_INFO() << "✅ Device and queue ready";

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateCommandPool failed";
        cleanup();
        return false;
    }

    VkCommandBufferAllocateInfo cmd_alloc = {};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = command_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cmd_alloc, &command_buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateCommandBuffers failed";
        cleanup();
        return false;
    }

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fence_info, nullptr, &render_fence) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateFence failed";
        cleanup();
        return false;
    }

    VkShaderModuleCreateInfo shader_info = {};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = sizeof(kVertexShaderSpv);
    shader_info.pCode = kVertexShaderSpv;
    if (vkCreateShaderModule(device, &shader_info, nullptr, &vert_shader) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create vertex shader module";
        cleanup();
        return false;
    }
    shader_info.codeSize = sizeof(kFragmentShaderSpv);
    shader_info.pCode = kFragmentShaderSpv;
    if (vkCreateShaderModule(device, &shader_info, nullptr, &frag_shader) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create fragment shader module";
        cleanup();
        return false;
    }
    TEST_LOG_INFO() << "✅ Shader modules created";

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreatePipelineLayout failed";
        cleanup();
        return false;
    }

    VkAttachmentDescription color_attachment = {};
    color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &color_attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    if (vkCreateRenderPass(device, &rp_info, nullptr, &render_pass) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateRenderPass failed";
        cleanup();
        return false;
    }

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {kImageWidth, kImageHeight, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &image_info, nullptr, &color_image.image) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateImage failed";
        cleanup();
        return false;
    }

    VkMemoryRequirements img_reqs = {};
    vkGetImageMemoryRequirements(device, color_image.image, &img_reqs);
    VkPhysicalDeviceMemoryProperties mem_props = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    uint32_t img_mem_type = find_memory_type(img_reqs.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             mem_props);
    if (img_mem_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ Unable to find memory type for image";
        cleanup();
        return false;
    }

    VkMemoryAllocateInfo img_alloc = {};
    img_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_alloc.allocationSize = img_reqs.size;
    img_alloc.memoryTypeIndex = img_mem_type;
    if (vkAllocateMemory(device, &img_alloc, nullptr, &color_image.memory) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateMemory failed for image";
        cleanup();
        return false;
    }
    vkBindImageMemory(device, color_image.image, color_image.memory, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = color_image.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_info.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_info, nullptr, &color_view) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateImageView failed";
        cleanup();
        return false;
    }

    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &color_view;
    fb_info.width = kImageWidth;
    fb_info.height = kImageHeight;
    fb_info.layers = 1;
    if (vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateFramebuffer failed";
        cleanup();
        return false;
    }

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(Vertex) * kVertices.size();
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &buffer_info, nullptr, &vertex_buffer.buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create vertex buffer";
        cleanup();
        return false;
    }
    VkMemoryRequirements vb_reqs = {};
    vkGetBufferMemoryRequirements(device, vertex_buffer.buffer, &vb_reqs);
    uint32_t vb_mem_type = find_memory_type(vb_reqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            mem_props);
    if (vb_mem_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ Unable to find memory type for vertex buffer";
        cleanup();
        return false;
    }
    VkMemoryAllocateInfo vb_alloc = {};
    vb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vb_alloc.allocationSize = vb_reqs.size;
    vb_alloc.memoryTypeIndex = vb_mem_type;
    if (vkAllocateMemory(device, &vb_alloc, nullptr, &vertex_buffer.memory) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to allocate vertex buffer memory";
        cleanup();
        return false;
    }
    vkBindBufferMemory(device, vertex_buffer.buffer, vertex_buffer.memory, 0);
    void* mapped = nullptr;
    vkMapMemory(device, vertex_buffer.memory, 0, buffer_info.size, 0, &mapped);
    std::memcpy(mapped, kVertices.data(), buffer_info.size);
    if (!flush_memory(device, vertex_buffer.memory, buffer_info.size)) {
        TEST_LOG_ERROR() << "✗ Failed to flush vertex buffer memory";
        cleanup();
        return false;
    }
    vkUnmapMemory(device, vertex_buffer.memory);

    buffer_info.size = static_cast<VkDeviceSize>(kImageWidth) * kImageHeight * 4u;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(device, &buffer_info, nullptr, &readback_buffer.buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create readback buffer";
        cleanup();
        return false;
    }
    VkMemoryRequirements rb_reqs = {};
    vkGetBufferMemoryRequirements(device, readback_buffer.buffer, &rb_reqs);
    uint32_t rb_mem_type = find_memory_type(rb_reqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            mem_props);
    if (rb_mem_type == UINT32_MAX) {
        TEST_LOG_ERROR() << "✗ Unable to find memory type for readback buffer";
        cleanup();
        return false;
    }
    VkMemoryAllocateInfo rb_alloc = {};
    rb_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    rb_alloc.allocationSize = rb_reqs.size;
    rb_alloc.memoryTypeIndex = rb_mem_type;
    if (vkAllocateMemory(device, &rb_alloc, nullptr, &readback_buffer.memory) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to allocate readback buffer memory";
        cleanup();
        return false;
    }
    vkBindBufferMemory(device, readback_buffer.buffer, readback_buffer.memory, 0);

    VkPipelineShaderStageCreateInfo stage_infos[2] = {};
    stage_infos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_infos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage_infos[0].module = vert_shader;
    stage_infos[0].pName = "main";
    stage_infos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_infos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stage_infos[1].module = frag_shader;
    stage_infos[1].pName = "main";

    VkVertexInputBindingDescription binding_desc = {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attribs = {};
    attribs[0].location = 0;
    attribs[0].binding = 0;
    attribs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[0].offset = offsetof(Vertex, position);
    attribs[1].location = 1;
    attribs[1].binding = 0;
    attribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertex_input.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(kImageWidth);
    viewport.height = static_cast<float>(kImageHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {kImageWidth, kImageHeight};

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend = {};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stage_infos;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateGraphicsPipelines failed";
        cleanup();
        return false;
    }
    TEST_LOG_INFO() << "✅ Graphics pipeline created";

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkBeginCommandBuffer failed";
        cleanup();
        return false;
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = color_image.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    VkClearValue clear_color = {};
    clear_color.color = {{0.05f, 0.05f, 0.05f, 1.0f}};

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {kImageWidth, kImageHeight};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    vkCmdBeginRenderPass(command_buffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offsets = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &offsets);
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdDraw(command_buffer, static_cast<uint32_t>(kVertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);

    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    VkBufferImageCopy copy_region = {};
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = {kImageWidth, kImageHeight, 1};

    vkCmdCopyImageToBuffer(command_buffer,
                           color_image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer.buffer,
                           1,
                           &copy_region);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkEndCommandBuffer failed";
        cleanup();
        return false;
    }

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    if (vkQueueSubmit(queue, 1, &submit_info, render_fence) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkQueueSubmit failed";
        cleanup();
        return false;
    }

    if (vkWaitForFences(device, 1, &render_fence, VK_TRUE, std::numeric_limits<uint64_t>::max()) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkWaitForFences failed";
        cleanup();
        return false;
    }
    TEST_LOG_INFO() << "✅ Rendering completed";

    std::vector<uint8_t> pixels(buffer_info.size);
    void* read_ptr = nullptr;
    if (vkMapMemory(device, readback_buffer.memory, 0, buffer_info.size, 0, &read_ptr) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkMapMemory failed for readback buffer";
        cleanup();
        return false;
    }
    if (!invalidate_memory(device, readback_buffer.memory, buffer_info.size)) {
        TEST_LOG_ERROR() << "✗ Failed to invalidate readback buffer";
        vkUnmapMemory(device, readback_buffer.memory);
        cleanup();
        return false;
    }
    std::memcpy(pixels.data(), read_ptr, buffer_info.size);
    vkUnmapMemory(device, readback_buffer.memory);

    const uint32_t center_x = kImageWidth / 2;
    const uint32_t center_y = kImageHeight / 2;
    const uint8_t* center_pixel = pixels.data() + (center_y * kImageWidth + center_x) * 4;
    if (center_pixel[0] < 32 && center_pixel[1] < 32 && center_pixel[2] < 32) {
        TEST_LOG_ERROR() << "✗ Center pixel is too dark, triangle likely missing";
        cleanup();
        return false;
    }

    const std::string output_path = "triangle.png";
    if (!write_png(output_path, kImageWidth, kImageHeight, pixels)) {
        TEST_LOG_ERROR() << "✗ Failed to write triangle.png";
        cleanup();
        return false;
    }

    TEST_LOG_INFO() << "✅ Saved rendered image to " << output_path;

    // Exercise swapchain protocol (headless WSI)
    const uint32_t kSwapchainWidth = 128;
    const uint32_t kSwapchainHeight = 128;
    std::set<std::string> files_before = list_swapchain_files();

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = VK_NULL_HANDLE;
    swapchain_info.minImageCount = 2;
    swapchain_info.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    swapchain_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchain_info.imageExtent = {kSwapchainWidth, kSwapchainHeight};
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;

    VkResult swap_result = vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain_handle);
    if (swap_result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateSwapchainKHR failed: " << swap_result;
        cleanup();
        return false;
    }

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain_handle, &swapchain_image_count, nullptr);
    std::vector<VkImage> swapchain_images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain_handle, &swapchain_image_count, swapchain_images.data());

    uint32_t image_index = 0;
    swap_result = vkAcquireNextImageKHR(device,
                                        swapchain_handle,
                                        UINT64_MAX,
                                        VK_NULL_HANDLE,
                                        VK_NULL_HANDLE,
                                        &image_index);
    if (swap_result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAcquireNextImageKHR failed: " << swap_result;
        cleanup();
        return false;
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_handle;
    present_info.pImageIndices = &image_index;

    swap_result = vkQueuePresentKHR(queue, &present_info);
    if (swap_result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkQueuePresentKHR failed: " << swap_result;
        cleanup();
        return false;
    }

    std::string new_frame_path;
    if (!wait_for_new_frame(files_before, &new_frame_path, std::chrono::seconds(2))) {
        TEST_LOG_ERROR() << "✗ Timed out waiting for headless frame output";
        cleanup();
        return false;
    }

    if (!validate_frame_file(new_frame_path, kSwapchainWidth, kSwapchainHeight)) {
        TEST_LOG_ERROR() << "✗ Swapchain frame validation failed (" << new_frame_path << ")";
        cleanup();
        return false;
    }

    std::filesystem::remove(new_frame_path);
    TEST_LOG_INFO() << "✅ Swapchain present produced headless frame file";

    TEST_LOG_INFO() << "✅ Phase 10 PASSED";

    cleanup();
    return true;
}
