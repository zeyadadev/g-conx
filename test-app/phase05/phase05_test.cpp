#include "phase05_test.h"

#include "logging.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace {

int32_t find_memory_type(uint32_t type_bits,
                         VkMemoryPropertyFlags required,
                         const VkPhysicalDeviceMemoryProperties& properties) {
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

struct BufferResources {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

bool create_buffer(VkDevice device,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   uint32_t memory_type,
                   BufferResources* out) {
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, nullptr, &out->buffer) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to create buffer of size " << size << "\n";
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, out->buffer, &requirements);

    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    if (vkAllocateMemory(device, &alloc_info, nullptr, &out->memory) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to allocate buffer memory\n";
        vkDestroyBuffer(device, out->buffer, nullptr);
        out->buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(device, out->buffer, out->memory, 0) != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ Failed to bind buffer memory\n";
        vkDestroyBuffer(device, out->buffer, nullptr);
        vkFreeMemory(device, out->memory, nullptr);
        out->buffer = VK_NULL_HANDLE;
        out->memory = VK_NULL_HANDLE;
        return false;
    }

    out->size = size;
    return true;
}

void destroy_buffer(VkDevice device, BufferResources& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
}

} // namespace

bool run_phase05_test() {
    TEST_LOG_INFO() << "\n========================================\n";
    TEST_LOG_INFO() << "Phase 5: Fake Command Recording\n";
    TEST_LOG_INFO() << "========================================\n\n";

    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    BufferResources src_buffer;
    BufferResources dst_buffer;
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    bool success = false;

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phase 5 Test";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "VenusPlus";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateInstance failed: " << result << "\n";
        return false;
    }
    TEST_LOG_INFO() << "✅ Instance created\n";

    uint32_t device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        TEST_LOG_ERROR() << "✗ Failed to enumerate physical devices\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkEnumeratePhysicalDevices failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    physical_device = physical_devices[0];
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    result = vkCreateDevice(physical_device, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateDevice failed: " << result << "\n";
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✅ Device created\n";

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = 0;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    result = vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkCreateCommandPool failed: " << result << "\n";
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✅ vkCreateCommandPool succeeded\n";

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "✗ vkAllocateCommandBuffers failed: " << result << "\n";
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }
    TEST_LOG_INFO() << "✅ vkAllocateCommandBuffers (1 buffer) succeeded\n";

    const VkDeviceSize buffer_size = 1024ull;
    uint32_t host_visible_type = 0;
    bool found_type = false;

    VkBufferCreateInfo temp_info = {};
    temp_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    temp_info.size = buffer_size;
    temp_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    temp_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer temp_buffer = VK_NULL_HANDLE;
    vkCreateBuffer(device, &temp_info, nullptr, &temp_buffer);
    VkMemoryRequirements temp_requirements;
    vkGetBufferMemoryRequirements(device, temp_buffer, &temp_requirements);

    int32_t mem_index =
        find_memory_type(temp_requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         memory_properties);
    if (mem_index >= 0) {
        host_visible_type = static_cast<uint32_t>(mem_index);
        found_type = true;
    }
    vkDestroyBuffer(device, temp_buffer, nullptr);

    if (!found_type) {
        TEST_LOG_ERROR() << "✗ Unable to find suitable memory type\n";
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    do {
        if (!create_buffer(device,
                           buffer_size,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           host_visible_type,
                           &src_buffer)) {
            break;
        }

        if (!create_buffer(device,
                           buffer_size,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           host_visible_type,
                           &dst_buffer)) {
            break;
        }
        TEST_LOG_INFO() << "✅ Source and destination buffers created\n";

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkBeginCommandBuffer failed: " << result << "\n";
            break;
        }
        TEST_LOG_INFO() << "✅ vkBeginCommandBuffer succeeded\n";

        vkCmdFillBuffer(command_buffer, src_buffer.buffer, 0, buffer_size, 0xDEADBEEF);
        TEST_LOG_INFO() << "✅ vkCmdFillBuffer recorded\n";

        VkBufferCopy region = {};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = buffer_size;
        vkCmdCopyBuffer(command_buffer, src_buffer.buffer, dst_buffer.buffer, 1, &region);
        TEST_LOG_INFO() << "✅ vkCmdCopyBuffer recorded\n";

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            TEST_LOG_ERROR() << "✗ vkEndCommandBuffer failed: " << result << "\n";
            break;
        }
        TEST_LOG_INFO() << "✅ vkEndCommandBuffer succeeded\n";

        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        command_buffer = VK_NULL_HANDLE;
        TEST_LOG_INFO() << "✅ Command buffer freed\n";

        vkDestroyCommandPool(device, command_pool, nullptr);
        command_pool = VK_NULL_HANDLE;
        TEST_LOG_INFO() << "✅ Command pool destroyed\n";

        TEST_LOG_INFO() << "✅ Command buffer state: EXECUTABLE (recording succeeded)\n";
        TEST_LOG_INFO() << "✅ Cleanup succeeded\n";
        TEST_LOG_INFO() << "✅ Phase 5 PASSED\n";
        success = true;
    } while (false);

    if (command_buffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }
    destroy_buffer(device, src_buffer);
    destroy_buffer(device, dst_buffer);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return success;
}
