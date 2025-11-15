#include "icd_entrypoints.h"
#include "network/network_client.h"
#include <iostream>
#include <vector>
#include <cstring>

// For Phase 1, we'll use a simple global connection
static venus_plus::NetworkClient g_client;
static bool g_connected = false;

// Constructor - runs when the shared library is loaded
__attribute__((constructor))
static void icd_init() {
    std::cout << "\n===========================================\n";
    std::cout << "VENUS PLUS ICD LOADED!\n";
    std::cout << "===========================================\n\n";
}

static bool ensure_connected() {
    if (!g_connected) {
        // TODO: Get host/port from env variable
        if (!g_client.connect("127.0.0.1", 5556)) {
            return false;
        }
        g_connected = true;
    }
    return true;
}

extern "C" {

// ICD interface version negotiation
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    std::cout << "[Client ICD] vk_icdNegotiateLoaderICDInterfaceVersion called\n";
    std::cout << "[Client ICD] Loader requested version: " << *pSupportedVersion << "\n";

    // We support ICD interface version 5
    if (*pSupportedVersion > 5) {
        *pSupportedVersion = 5;
    }

    std::cout << "[Client ICD] Negotiated version: " << *pSupportedVersion << "\n";
    return VK_SUCCESS;
}

// ICD GetInstanceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    std::cout << "[Client ICD] vk_icdGetInstanceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        std::cout << " -> returning nullptr\n";
        return nullptr;
    }

    // Return our implementations
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        std::cout << " -> returning vkEnumerateInstanceVersion\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        std::cout << " -> returning vkEnumerateInstanceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        std::cout << " -> returning vkGetInstanceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }

    std::cout << " -> NOT FOUND, returning nullptr\n";
    return nullptr;
}

// Standard vkGetInstanceProcAddr (required by spec)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    std::cout << "[Client ICD] vkEnumerateInstanceVersion called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Failed to connect to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // For Phase 1: Send a simple encoded message
    // Format: [command_type: uint32_t]
    uint32_t command_type = 1;  // Arbitrary ID for EnumerateInstanceVersion

    if (!g_client.send(&command_type, sizeof(command_type))) {
        std::cerr << "[Client ICD] Failed to send command\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client ICD] Sent command\n";

    // Receive reply
    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        std::cerr << "[Client ICD] Failed to receive reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client ICD] Received reply: " << reply.size() << " bytes\n";

    // Decode reply: [result: uint32_t][version: uint32_t]
    if (reply.size() < 8) {
        std::cerr << "[Client ICD] Invalid reply size\n";
        return VK_ERROR_DEVICE_LOST;
    }

    uint32_t result;
    uint32_t version;
    memcpy(&result, reply.data(), 4);
    memcpy(&version, reply.data() + 4, 4);

    if (result != VK_SUCCESS) {
        return static_cast<VkResult>(result);
    }

    *pApiVersion = version;
    std::cout << "[Client ICD] Version: " << version << "\n";

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    std::cout << "[Client ICD] vkEnumerateInstanceExtensionProperties called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client ICD] Failed to connect to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // For Phase 1: Send command and get fake reply
    uint32_t command_type = 2;  // EnumerateInstanceExtensionProperties

    if (!g_client.send(&command_type, sizeof(command_type))) {
        std::cerr << "[Client ICD] Failed to send command\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client ICD] Sent command\n";

    // Receive reply
    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        std::cerr << "[Client ICD] Failed to receive reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client ICD] Received reply: " << reply.size() << " bytes\n";

    // Decode reply: [result: uint32_t][count: uint32_t]
    if (reply.size() < 8) {
        std::cerr << "[Client ICD] Invalid reply size\n";
        return VK_ERROR_DEVICE_LOST;
    }

    uint32_t result;
    uint32_t count;
    memcpy(&result, reply.data(), 4);
    memcpy(&count, reply.data() + 4, 4);

    if (result != VK_SUCCESS) {
        return static_cast<VkResult>(result);
    }

    *pPropertyCount = count;
    std::cout << "[Client ICD] Extension count: " << count << "\n";

    // For Phase 1, we don't fill in actual extension properties
    // Phase 2 will implement this properly

    return VK_SUCCESS;
}

} // extern "C"
