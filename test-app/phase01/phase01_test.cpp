#include "phase01_test.h"
#include "logging.h"
#include <vulkan/vulkan.h>

namespace phase01 {

int run_test() {
    TEST_LOG_INFO() << "";
    TEST_LOG_INFO() << "=================================================";
    TEST_LOG_INFO() << "Phase 1: Network Communication";
    TEST_LOG_INFO() << "=================================================";

    // Test 1: vkEnumerateInstanceVersion (doesn't load ICD)
    TEST_LOG_INFO() << "Test 1: vkEnumerateInstanceVersion";
    uint32_t version = 0;
    VkResult result = vkEnumerateInstanceVersion(&version);

    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "FAILED: vkEnumerateInstanceVersion returned " << result;
        return 1;
    }

    uint32_t major = VK_VERSION_MAJOR(version);
    uint32_t minor = VK_VERSION_MINOR(version);
    uint32_t patch = VK_VERSION_PATCH(version);

    TEST_LOG_INFO() << "  Version: " << major << "." << minor << "." << patch;
    TEST_LOG_INFO() << "  (Note: Loader answers this without loading ICD)";

    // Test 2: vkEnumerateInstanceExtensionProperties (DOES load ICD)
    TEST_LOG_INFO() << "Test 2: vkEnumerateInstanceExtensionProperties (forces ICD load)";
    uint32_t extension_count = 0;
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    if (result != VK_SUCCESS) {
        TEST_LOG_ERROR() << "FAILED: vkEnumerateInstanceExtensionProperties returned " << result;
        return 1;
    }

    TEST_LOG_INFO() << "  Extension count: " << extension_count;

    TEST_LOG_INFO() << "";
    TEST_LOG_INFO() << "Phase 1 PASSED";
    TEST_LOG_INFO() << "=================================================";

    return 0;
}

} // namespace phase01
