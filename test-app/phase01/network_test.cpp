#include "logging.h"
#include <vulkan/vulkan.h>
#include "network/network_client.h"
#include <cstring>
#include <vector>

using namespace venus_plus;

int main() {
    TEST_LOG_INFO() << "\n";
    TEST_LOG_INFO() << "===========================================\n";
    TEST_LOG_INFO() << "Phase 1: Direct Network Communication Test\n";
    TEST_LOG_INFO() << "===========================================\n\n";

    // Test 1: Connect to server
    TEST_LOG_INFO() << "Test 1: Connecting to server...\n";
    NetworkClient client;
    if (!client.connect("127.0.0.1", 5556)) {
        TEST_LOG_ERROR() << "FAILED: Could not connect to server\n";
        return 1;
    }
    TEST_LOG_INFO() << "  SUCCESS: Connected to server\n\n";

    // Test 2: Send vkEnumerateInstanceVersion command
    TEST_LOG_INFO() << "Test 2: Sending vkEnumerateInstanceVersion command...\n";
    uint32_t command_type = 1;
    if (!client.send(&command_type, sizeof(command_type))) {
        TEST_LOG_ERROR() << "FAILED: Could not send command\n";
        return 1;
    }
    TEST_LOG_INFO() << "  SUCCESS: Command sent\n\n";

    // Test 3: Receive reply
    TEST_LOG_INFO() << "Test 3: Receiving reply...\n";
    std::vector<uint8_t> reply;
    if (!client.receive(reply)) {
        TEST_LOG_ERROR() << "FAILED: Could not receive reply\n";
        return 1;
    }
    TEST_LOG_INFO() << "  SUCCESS: Received " << reply.size() << " bytes\n\n";

    // Test 4: Decode reply
    TEST_LOG_INFO() << "Test 4: Decoding reply...\n";
    if (reply.size() < 8) {
        TEST_LOG_ERROR() << "FAILED: Reply too small\n";
        return 1;
    }

    uint32_t result, version;
    memcpy(&result, reply.data(), 4);
    memcpy(&version, reply.data() + 4, 4);

    TEST_LOG_INFO() << "  Result: " << result << " (VK_SUCCESS=" << VK_SUCCESS << ")\n";
    TEST_LOG_INFO() << "  Version: " << VK_VERSION_MAJOR(version) << "."
              << VK_VERSION_MINOR(version) << "."
              << VK_VERSION_PATCH(version) << "\n\n";

    if (result == VK_SUCCESS && version == VK_API_VERSION_1_3) {
        TEST_LOG_INFO() << "===========================================\n";
        TEST_LOG_INFO() << "ALL TESTS PASSED!\n";
        TEST_LOG_INFO() << "Network communication is working correctly.\n";
        TEST_LOG_INFO() << "===========================================\n\n";
        return 0;
    } else {
        TEST_LOG_ERROR() << "FAILED: Unexpected result or version\n";
        return 1;
    }
}
