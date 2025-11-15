#include <iostream>
#include <vulkan/vulkan.h>
#include "network/network_client.h"
#include <cstring>
#include <vector>

using namespace venus_plus;

int main() {
    std::cout << "\n";
    std::cout << "===========================================\n";
    std::cout << "Phase 1: Direct Network Communication Test\n";
    std::cout << "===========================================\n\n";

    // Test 1: Connect to server
    std::cout << "Test 1: Connecting to server...\n";
    NetworkClient client;
    if (!client.connect("127.0.0.1", 5556)) {
        std::cerr << "FAILED: Could not connect to server\n";
        return 1;
    }
    std::cout << "  SUCCESS: Connected to server\n\n";

    // Test 2: Send vkEnumerateInstanceVersion command
    std::cout << "Test 2: Sending vkEnumerateInstanceVersion command...\n";
    uint32_t command_type = 1;
    if (!client.send(&command_type, sizeof(command_type))) {
        std::cerr << "FAILED: Could not send command\n";
        return 1;
    }
    std::cout << "  SUCCESS: Command sent\n\n";

    // Test 3: Receive reply
    std::cout << "Test 3: Receiving reply...\n";
    std::vector<uint8_t> reply;
    if (!client.receive(reply)) {
        std::cerr << "FAILED: Could not receive reply\n";
        return 1;
    }
    std::cout << "  SUCCESS: Received " << reply.size() << " bytes\n\n";

    // Test 4: Decode reply
    std::cout << "Test 4: Decoding reply...\n";
    if (reply.size() < 8) {
        std::cerr << "FAILED: Reply too small\n";
        return 1;
    }

    uint32_t result, version;
    memcpy(&result, reply.data(), 4);
    memcpy(&version, reply.data() + 4, 4);

    std::cout << "  Result: " << result << " (VK_SUCCESS=" << VK_SUCCESS << ")\n";
    std::cout << "  Version: " << VK_VERSION_MAJOR(version) << "."
              << VK_VERSION_MINOR(version) << "."
              << VK_VERSION_PATCH(version) << "\n\n";

    if (result == VK_SUCCESS && version == VK_API_VERSION_1_3) {
        std::cout << "===========================================\n";
        std::cout << "ALL TESTS PASSED!\n";
        std::cout << "Network communication is working correctly.\n";
        std::cout << "===========================================\n\n";
        return 0;
    } else {
        std::cerr << "FAILED: Unexpected result or version\n";
        return 1;
    }
}
