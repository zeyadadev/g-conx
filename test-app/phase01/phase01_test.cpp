#include "phase01_test.h"
#include <vulkan/vulkan.h>
#include <iostream>

namespace phase01 {

int run_test() {
    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "Phase 1: Network Communication\n";
    std::cout << "=================================================\n\n";

    // Test 1: vkEnumerateInstanceVersion (doesn't load ICD)
    std::cout << "Test 1: vkEnumerateInstanceVersion\n";
    uint32_t version = 0;
    VkResult result = vkEnumerateInstanceVersion(&version);

    if (result != VK_SUCCESS) {
        std::cerr << "FAILED: vkEnumerateInstanceVersion returned " << result << "\n";
        return 1;
    }

    uint32_t major = VK_VERSION_MAJOR(version);
    uint32_t minor = VK_VERSION_MINOR(version);
    uint32_t patch = VK_VERSION_PATCH(version);

    std::cout << "  Version: " << major << "." << minor << "." << patch << "\n";
    std::cout << "  (Note: Loader answers this without loading ICD)\n\n";

    // Test 2: vkEnumerateInstanceExtensionProperties (DOES load ICD)
    std::cout << "Test 2: vkEnumerateInstanceExtensionProperties (forces ICD load)\n";
    uint32_t extension_count = 0;
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    if (result != VK_SUCCESS) {
        std::cerr << "FAILED: vkEnumerateInstanceExtensionProperties returned " << result << "\n";
        return 1;
    }

    std::cout << "  Extension count: " << extension_count << "\n";

    std::cout << "\n";
    std::cout << "Phase 1 PASSED\n";
    std::cout << "=================================================\n\n";

    return 0;
}

} // namespace phase01
