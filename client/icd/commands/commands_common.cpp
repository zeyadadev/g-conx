// Common implementation file for ICD commands
// Global variable definitions

#include "commands_common.h"
#include "profiling.h"
#include <csignal>
#include <cstdlib>

// Global connection state definitions
NetworkClient g_client;
vn_ring g_ring = {};
bool g_connected = false;

// Signal handler to print profiling on Ctrl+C
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        ICD_LOG_INFO() << "\n\nReceived interrupt signal, printing profiling data...\n";
        VENUS_PROFILE_PRINT();
        std::exit(signum);
    }
}

// Constructor - runs when the shared library is loaded
__attribute__((constructor))
static void icd_init() {
    ICD_LOG_INFO() << "\n===========================================\n";
    ICD_LOG_INFO() << "VENUS PLUS ICD LOADED!\n";
    ICD_LOG_INFO() << "===========================================\n\n";

    // Register signal handlers for Ctrl+C
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    VENUS_PROFILE_START();
}

// Destructor - runs when the shared library is unloaded
__attribute__((destructor))
static void icd_cleanup() {
    ICD_LOG_INFO() << "\n===========================================\n";
    ICD_LOG_INFO() << "VENUS PLUS ICD UNLOADING\n";
    ICD_LOG_INFO() << "===========================================\n\n";

    VENUS_PROFILE_PRINT();
}
