# Venus Plus Project Structure

**Detailed directory layout and file organization**

## Table of Contents

1. [Overview](#overview)
2. [Directory Tree](#directory-tree)
3. [Component Details](#component-details)
4. [Build Outputs](#build-outputs)
5. [File Naming Conventions](#file-naming-conventions)

## Overview

The Venus Plus project is organized into clear, modular components:

- **`common/`**: Shared code used by both client and server
- **`client/`**: ICD implementation (client-side)
- **`server/`**: Server implementation (GPU executor)
- **`test-app/`**: Growing Vulkan test application (C++)
- **`docs/`**: All documentation
- **`build/`**: CMake build output (generated)

## Directory Tree

```
venus-plus/
│
├── README.md                           # Project overview
├── LICENSE                             # Project license (TBD)
├── .gitignore                          # Git ignore rules
├── CMakeLists.txt                      # Root CMake configuration
│
├── docs/                               # Documentation
│   ├── ARCHITECTURE.md                 # System architecture
│   ├── PROJECT_STRUCTURE.md            # This file
│   ├── DEVELOPMENT_ROADMAP.md          # Phase-by-phase plan
│   ├── TESTING_STRATEGY.md             # Testing approach
│   ├── BUILD_AND_RUN.md                # Build and run guide
│   ├── PHASE_01.md                     # Phase 1: Network communication
│   ├── PHASE_02.md                     # Phase 2: Fake instance
│   ├── PHASE_03.md                     # Phase 3: Fake device
│   ├── PHASE_04.md                     # Phase 4: Fake resources
│   ├── PHASE_05.md                     # Phase 5: Fake commands
│   ├── PHASE_06.md                     # Phase 6: Fake submission
│   ├── PHASE_07.md                     # Phase 7: Real execution
│   ├── PHASE_08.md                     # Phase 8: Memory transfer
│   ├── PHASE_09.md                     # Phase 9: Compute shaders
│   ├── PHASE_10.md                     # Phase 10: Graphics rendering
│   └── TROUBLESHOOTING.md              # Common issues and solutions
│
├── common/                             # Shared code
│   ├── CMakeLists.txt                  # Common library build
│   │
│   ├── venus-protocol/                 # Venus protocol headers (from Mesa)
│   │   ├── README.md                   # Source and version info
│   │   ├── vn_protocol_driver.h        # Main protocol header
│   │   ├── vn_protocol_driver_cs.h     # Command stream primitives
│   │   ├── vn_protocol_driver_types.h  # Type definitions
│   │   ├── vn_protocol_driver_handles.h # Handle encoding/decoding
│   │   ├── vn_protocol_driver_structs.h # Struct encoding/decoding
│   │   ├── vn_protocol_driver_instance.h # Instance commands
│   │   ├── vn_protocol_driver_device.h  # Device commands
│   │   ├── vn_protocol_driver_queue.h   # Queue commands
│   │   ├── vn_protocol_driver_buffer.h  # Buffer commands
│   │   ├── vn_protocol_driver_image.h   # Image commands
│   │   ├── vn_protocol_driver_command_buffer.h # Command buffer commands
│   │   └── ...                         # Other command headers
│   │
│   ├── protocol-helpers/               # Venus protocol C++ wrappers
│   │   ├── CMakeLists.txt
│   │   ├── venus_encoder.h             # Encoding helper class
│   │   ├── venus_encoder.cpp
│   │   ├── venus_decoder.h             # Decoding helper class
│   │   ├── venus_decoder.cpp
│   │   └── venus_types.h               # Common type definitions
│   │
│   ├── network/                        # TCP networking utilities
│   │   ├── CMakeLists.txt
│   │   ├── network_client.h            # Client-side networking
│   │   ├── network_client.cpp
│   │   ├── network_server.h            # Server-side networking
│   │   ├── network_server.cpp
│   │   ├── message.h                   # Message protocol
│   │   ├── message.cpp
│   │   └── socket_utils.h              # Low-level socket helpers
│   │   └── socket_utils.cpp
│   │
│   └── utils/                          # General utilities
│       ├── CMakeLists.txt
│       ├── logging.h                   # Logging utilities
│       ├── logging.cpp
│       ├── handle_map.h                # Handle mapping template
│       └── handle_map.cpp
│
├── client/                             # ICD implementation
│   ├── CMakeLists.txt                  # Client build configuration
│   ├── venus_icd.json.in               # ICD manifest template
│   │
│   ├── icd/                            # Core ICD code
│   │   ├── icd_entrypoints.h           # Vulkan API entrypoints
│   │   ├── icd_entrypoints.cpp
│   │   ├── icd_dispatch.h              # Dispatch table
│   │   ├── icd_dispatch.cpp
│   │   ├── icd_globals.h               # Global state
│   │   └── icd_globals.cpp
│   │
│   ├── commands/                       # Vulkan command implementations
│   │   ├── instance.h                  # Instance commands
│   │   ├── instance.cpp                # vkCreateInstance, etc.
│   │   ├── physical_device.h           # Physical device commands
│   │   ├── physical_device.cpp         # vkEnumeratePhysicalDevices, etc.
│   │   ├── device.h                    # Device commands
│   │   ├── device.cpp                  # vkCreateDevice, etc.
│   │   ├── queue.h                     # Queue commands
│   │   ├── queue.cpp                   # vkQueueSubmit, etc.
│   │   ├── resources.h                 # Resource commands
│   │   ├── resources.cpp               # vkCreateBuffer, vkCreateImage, etc.
│   │   ├── memory.h                    # Memory commands
│   │   ├── memory.cpp                  # vkAllocateMemory, vkMapMemory, etc.
│   │   ├── sync.h                      # Synchronization commands
│   │   ├── sync.cpp                    # vkCreateFence, vkWaitForFences, etc.
│   │   ├── command_buffer.h            # Command buffer commands
│   │   ├── command_buffer.cpp          # vkBeginCommandBuffer, vkCmd*, etc.
│   │   ├── pipeline.h                  # Pipeline commands
│   │   ├── pipeline.cpp                # vkCreateGraphicsPipelines, etc.
│   │   └── ...                         # Other command categories
│   │
│   ├── state/                          # Client-side state management
│   │   ├── instance_state.h            # Instance state
│   │   ├── instance_state.cpp
│   │   ├── device_state.h              # Device state
│   │   ├── device_state.cpp
│   │   ├── handle_allocator.h          # Client handle allocation
│   │   └── handle_allocator.cpp
│   │
│   └── tests/                          # Unit tests for client
│       ├── CMakeLists.txt
│       ├── test_encoder.cpp            # Test Venus encoding
│       ├── test_network.cpp            # Test network layer
│       └── test_handles.cpp            # Test handle allocation
│
├── server/                             # Server implementation
│   ├── CMakeLists.txt                  # Server build configuration
│   │
│   ├── main.cpp                        # Server entry point
│   ├── server.h                        # Main server class
│   ├── server.cpp
│   ├── client_connection.h             # Per-client connection handler
│   ├── client_connection.cpp
│   │
│   ├── decoder/                        # Venus protocol decoder
│   │   ├── command_decoder.h           # Command decoding
│   │   ├── command_decoder.cpp
│   │   ├── command_dispatcher.h        # Dispatch to handlers
│   │   └── command_dispatcher.cpp
│   │
│   ├── executor/                       # Vulkan execution engine
│   │   ├── vulkan_executor.h           # Main executor
│   │   ├── vulkan_executor.cpp
│   │   ├── instance_executor.h         # Instance command execution
│   │   ├── instance_executor.cpp
│   │   ├── device_executor.h           # Device command execution
│   │   ├── device_executor.cpp
│   │   ├── queue_executor.h            # Queue command execution
│   │   ├── queue_executor.cpp
│   │   ├── resource_executor.h         # Resource command execution
│   │   ├── resource_executor.cpp
│   │   └── ...                         # Other executors
│   │
│   ├── state/                          # Server-side state management
│   │   ├── server_state.h              # Global server state
│   │   ├── server_state.cpp
│   │   ├── client_state.h              # Per-client state
│   │   ├── client_state.cpp
│   │   ├── handle_map.h                # Client→Real handle mapping
│   │   ├── handle_map.cpp
│   │   ├── resource_tracker.h          # Track GPU resources
│   │   └── resource_tracker.cpp
│   │
│   └── tests/                          # Unit tests for server
│       ├── CMakeLists.txt
│       ├── test_decoder.cpp            # Test Venus decoding
│       ├── test_handle_map.cpp         # Test handle mapping
│       └── mock_client.cpp             # Mock client for testing
│
├── test-app/                           # Growing Vulkan test application
│   ├── CMakeLists.txt                  # Test app build
│   │
│   ├── main.cpp                        # Test app entry point
│   ├── app_base.h                      # Base application class
│   ├── app_base.cpp
│   │
│   ├── phase01/                        # Phase 1: Network test
│   │   ├── phase01_test.h
│   │   └── phase01_test.cpp            # Test: vkEnumerateInstanceVersion
│   │
│   ├── phase02/                        # Phase 2: Instance test
│   │   ├── phase02_test.h
│   │   └── phase02_test.cpp            # Test: vkCreateInstance
│   │
│   ├── phase03/                        # Phase 3: Device test
│   │   ├── phase03_test.h
│   │   └── phase03_test.cpp            # Test: vkCreateDevice
│   │
│   ├── phase04/                        # Phase 4: Resource test
│   │   ├── phase04_test.h
│   │   └── phase04_test.cpp            # Test: vkCreateBuffer
│   │
│   ├── phase05/                        # Phase 5: Command recording test
│   │   ├── phase05_test.h
│   │   └── phase05_test.cpp            # Test: vkCmdCopyBuffer
│   │
│   ├── phase06/                        # Phase 6: Submission test
│   │   ├── phase06_test.h
│   │   └── phase06_test.cpp            # Test: vkQueueSubmit
│   │
│   ├── phase07/                        # Phase 7: Real execution test
│   │   ├── phase07_test.h
│   │   └── phase07_test.cpp            # Test: Real buffer creation
│   │
│   ├── phase08/                        # Phase 8: Memory transfer test
│   │   ├── phase08_test.h
│   │   └── phase08_test.cpp            # Test: vkMapMemory/data transfer
│   │
│   ├── phase09/                        # Phase 9: Compute test
│   │   ├── phase09_test.h
│   │   ├── phase09_test.cpp            # Test: Compute shader
│   │   └── shaders/
│   │       └── simple_compute.comp     # Simple compute shader
│   │
│   ├── phase10/                        # Phase 10: Graphics test
│   │   ├── phase10_test.h
│   │   ├── phase10_test.cpp            # Test: Render triangle
│   │   └── shaders/
│   │       ├── triangle.vert           # Vertex shader
│   │       └── triangle.frag           # Fragment shader
│   │
│   └── utils/                          # Test app utilities
│       ├── test_helpers.h              # Helper functions
│       ├── test_helpers.cpp
│       ├── shader_loader.h             # Load SPIR-V shaders
│       └── shader_loader.cpp
│
└── build/                              # CMake build output (generated)
    ├── client/
    │   ├── libvenus_icd.so             # ICD shared library
    │   └── venus_icd.x86_64.json       # ICD manifest
    ├── server/
    │   └── venus-server                # Server executable
    └── test-app/
        └── venus-test-app              # Test application executable
```

## Component Details

### Common Library

**Purpose**: Shared utilities used by both client and server

**Key Components**:
1. **Venus Protocol** (`venus-protocol/`)
   - Copied from Mesa's generated headers
   - Provides encoding/decoding for all Vulkan commands
   - Updated when Vulkan API changes

2. **Protocol Helpers** (`protocol-helpers/`)
   - C++ wrappers around Venus C API
   - Simplifies encoding/decoding
   - RAII management of encoder/decoder state

3. **Network Layer** (`network/`)
   - TCP client/server abstractions
   - Message protocol implementation
   - Error handling and reconnection

4. **Utilities** (`utils/`)
   - Logging framework
   - Handle mapping templates
   - Common helper functions

### Client (ICD)

**Purpose**: Vulkan ICD that intercepts API calls and forwards to server

**Key Components**:
1. **ICD Core** (`icd/`)
   - Vulkan API entrypoints (exported functions)
   - Dispatch table management
   - Global ICD state

2. **Command Implementations** (`commands/`)
   - Each Vulkan command category in separate file
   - Commands encode parameters and send to server
   - Receive and decode server replies

3. **State Management** (`state/`)
   - Track client-side Vulkan objects
   - Allocate client handles
   - Maintain connection state

### Server

**Purpose**: Receives commands, executes on real GPU, returns results

**Key Components**:
1. **Server Core** (`main.cpp`, `server.cpp`)
   - TCP server setup
   - Client connection management
   - Multi-threaded client handling

2. **Decoder** (`decoder/`)
   - Decode Venus protocol messages
   - Dispatch to appropriate handlers
   - Validate commands

3. **Executor** (`executor/`)
   - Call real Vulkan API
   - Manage GPU resources
   - Return execution results

4. **State Management** (`state/`)
   - Map client handles to real handles
   - Track GPU resources per client
   - Clean up on client disconnect

### Test Application

**Purpose**: Incremental Vulkan test app that grows with each phase

**Structure**:
- Each phase adds new test code
- Tests build on previous phases
- Command-line selects which phase to run
- All phases remain runnable for regression testing

**Example Usage**:
```bash
# Run all phases sequentially
./venus-test-app --all

# Run specific phase
./venus-test-app --phase 3

# Run up to phase N
./venus-test-app --up-to 5
```

## Build Outputs

### Client Build Outputs

```
build/client/
├── libvenus_icd.so              # Shared library (Linux)
├── libvenus_icd.dylib           # Shared library (macOS)
├── venus_icd.dll                # Shared library (Windows)
└── venus_icd.x86_64.json        # ICD manifest
```

**ICD Manifest** (`venus_icd.x86_64.json`):
```json
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "/home/ayman/venus-plus/build/client/libvenus_icd.so",
        "api_version": "1.3.0"
    }
}
```

### Server Build Outputs

```
build/server/
└── venus-server                 # Server executable
```

### Test App Build Outputs

```
build/test-app/
├── venus-test-app               # Test application executable
└── shaders/                     # Compiled SPIR-V shaders
    ├── simple_compute.spv
    ├── triangle.vert.spv
    └── triangle.frag.spv
```

## File Naming Conventions

### C++ Files
- **Headers**: `.h` extension
- **Implementation**: `.cpp` extension
- **Snake case**: `my_class.h`, `my_class.cpp`

### Vulkan Commands
- Grouped by category: `instance.cpp`, `device.cpp`, `queue.cpp`
- One implementation per command: `vkCreateBuffer()` in `resources.cpp`

### Test Files
- Prefix with `test_`: `test_encoder.cpp`
- Phase tests: `phase01_test.cpp`, `phase02_test.cpp`

### Documentation
- All caps for major docs: `README.md`, `ARCHITECTURE.md`
- Phase docs numbered: `PHASE_01.md`, `PHASE_02.md`

### CMake Files
- One `CMakeLists.txt` per directory
- Root CMake includes subdirectories
- Libraries define targets (e.g., `venus_common`, `venus_client`)

## Adding New Files

### Adding a New Vulkan Command

1. **Client side**:
   ```bash
   # Edit appropriate file in client/commands/
   vim client/commands/pipeline.cpp

   # Add command implementation
   VkResult vkCreateGraphicsPipelines(...) {
       // Encode and send
   }
   ```

2. **Server side**:
   ```bash
   # Edit appropriate file in server/executor/
   vim server/executor/pipeline_executor.cpp

   # Add execution handler
   void handle_vkCreateGraphicsPipelines(...) {
       // Decode, execute, reply
   }
   ```

### Adding a New Test

1. **Create test file**:
   ```bash
   # Add to test-app/phaseXX/
   vim test-app/phase11/phase11_test.cpp
   ```

2. **Update CMakeLists.txt**:
   ```cmake
   # In test-app/CMakeLists.txt
   add_subdirectory(phase11)
   ```

3. **Update main.cpp**:
   ```cpp
   // In test-app/main.cpp
   case 11: return phase11::run_test();
   ```

## Version Control

### Git Structure

```
.git/
.gitignore                 # Ignore build/, .vscode/, etc.
.gitmodules               # If using submodules (future)
```

### .gitignore Contents

```gitignore
# Build outputs
build/
cmake-build-*/
*.o
*.so
*.dll
*.dylib

# IDE files
.vscode/
.idea/
*.swp
*~

# Logs
*.log

# Generated files
venus_icd.*.json
```

## IDE Configuration

### VSCode Recommended Settings

```json
{
    "cmake.configureOnOpen": true,
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "files.associations": {
        "*.h": "cpp"
    }
}
```

### Recommended VSCode Extensions
- CMake Tools
- C/C++
- Markdown All in One
- GitLens

---

**Next**: See [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) for the phase-by-phase implementation plan.
