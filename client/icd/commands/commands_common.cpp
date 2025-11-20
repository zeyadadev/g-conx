// Common implementation file for ICD commands
// Global variable definitions

#include "commands_common.h"

// Global connection state definitions
NetworkClient g_client;
vn_ring g_ring = {};
bool g_connected = false;

// Constructor - runs when the shared library is loaded
__attribute__((constructor))
static void icd_init() {
    ICD_LOG_INFO() << "\n===========================================\n";
    ICD_LOG_INFO() << "VENUS PLUS ICD LOADED!\n";
    ICD_LOG_INFO() << "===========================================\n\n";
}
