# Venus Plus - Network-Based Vulkan ICD

**A network-transparent Vulkan implementation that enables remote GPU rendering over TCP/IP**

## Overview

Venus Plus is a Vulkan Installable Client Driver (ICD) and server system that enables applications to use remote GPUs over a network connection. It consists of two main components:

1. **Client (ICD)**: A Vulkan ICD that intercepts Vulkan API calls, serializes them using the Venus protocol, and transmits them over TCP/IP
2. **Server**: A decoder that receives serialized Vulkan commands, executes them on a real GPU, and sends results back to the client

## Key Features

- **Network Transparent**: Applications use standard Vulkan API without modification
- **Venus Protocol**: Leverages the battle-tested Venus protocol for Vulkan command serialization
- **Incremental Development**: Built in phases with end-to-end testing from day one
- **Full Documentation**: Comprehensive documentation for each development phase
- **Cross-Platform**: Supports Linux (primary), with future Windows/macOS support planned

## Use Cases

- **Remote Rendering**: Run graphically intensive applications on a remote GPU
- **Cloud Gaming**: Stream games from powerful server GPUs to lightweight clients
- **Development/Testing**: Test Vulkan applications on different GPU hardware remotely
- **Resource Sharing**: Share expensive GPU hardware across multiple clients
- **Headless Rendering**: Render on servers without display hardware

## Architecture

```
┌─────────────────────┐                    ┌─────────────────────┐
│   Application       │                    │   Server            │
│                     │                    │                     │
│  Vulkan API Calls   │                    │  Real GPU           │
└──────────┬──────────┘                    └──────────▲──────────┘
           │                                          │
┌──────────▼──────────┐                    ┌──────────┴──────────┐
│  Venus Plus ICD     │                    │  Venus Plus Server  │
│                     │                    │                     │
│  - Encode commands  │    TCP/IP Socket   │  - Decode commands  │
│  - Venus Protocol   │◄──────────────────►│  - Execute Vulkan   │
│  - Network layer    │                    │  - Return results   │
└─────────────────────┘                    └─────────────────────┘
```

## Development Approach

This project follows an **incremental, test-driven development approach**:

- **Phase-based**: Development is divided into well-defined phases
- **End-to-End Testing**: Each phase includes working client-server communication
- **Progressive Complexity**: Start with simple commands, gradually add complexity
- **Continuous Documentation**: Document as we build, not after

### Development Phases

1. **Phase 1**: Network communication with Venus protocol (Days 1-3)
2. **Phase 2**: Fake instance creation (Days 4-7)
3. **Phase 3**: Fake device creation (Days 8-14)
4. **Phase 4**: Fake resource management (Days 15-21)
5. **Phase 5**: Fake command recording (Days 22-28)
6. **Phase 6**: Fake command submission (Days 29-35)
7. **Phase 7**: Real execution - simple commands (Days 36-49)
8. **Phase 8**: Memory transfer over network (Days 50-60)
9. **Phase 9**: Simple compute shaders (Days 61-75)
10. **Phase 10**: Simple graphics rendering (Days 76-90)

## Quick Start

### Prerequisites

- CMake 3.20+
- C++17 compiler (GCC 9+ or Clang 10+)
- Vulkan SDK 1.3+
- Linux (primary support)

### Building

```bash
cd /home/ayman/venus-plus
mkdir build && cd build
cmake ..
make
```

### Running

**Terminal 1 - Start the server:**
```bash
./server/venus-server --port 5556
```

**Terminal 2 - Run the test application:**
```bash
VK_DRIVER_FILES=/home/ayman/venus-plus/build/client/venus_icd.x86_64.json \
./test-app/venus-test-app
```

## Project Structure

```
venus-plus/
├── README.md                    # This file
├── docs/                        # Detailed documentation
│   ├── ARCHITECTURE.md          # System architecture
│   ├── PROJECT_STRUCTURE.md     # Directory layout
│   ├── DEVELOPMENT_ROADMAP.md   # Phase-by-phase plan
│   ├── TESTING_STRATEGY.md      # Testing approach
│   ├── BUILD_AND_RUN.md         # Build and run guide
│   ├── PHASE_01.md              # Phase 1 details
│   ├── PHASE_02.md              # Phase 2 details
│   └── ...                      # Other phase docs
├── common/                      # Shared code
│   ├── venus-protocol/          # Venus protocol headers (from Mesa)
│   ├── network/                 # TCP networking utilities
│   └── protocol-helpers/        # Venus encoding/decoding helpers
├── client/                      # ICD implementation
│   ├── icd/                     # Core ICD code
│   ├── commands/                # Vulkan command implementations
│   └── tests/                   # Unit tests
├── server/                      # Server implementation
│   ├── decoder/                 # Venus protocol decoder
│   ├── executor/                # Vulkan execution engine
│   ├── state/                   # Handle mapping and state
│   └── tests/                   # Server unit tests
├── test-app/                    # Growing Vulkan test application (C++)
│   ├── phase01/                 # Phase 1 test code
│   ├── phase02/                 # Phase 2 test code
│   └── ...                      # Other phases
└── CMakeLists.txt               # Root build configuration
```

## Documentation

All documentation is in the `docs/` directory:

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)**: Deep dive into system design
- **[DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md)**: Detailed phase-by-phase development plan
- **[TESTING_STRATEGY.md](docs/TESTING_STRATEGY.md)**: How we test the system
- **[BUILD_AND_RUN.md](docs/BUILD_AND_RUN.md)**: Complete build and run instructions
- **[PHASE_*.md](docs/)**: Detailed documentation for each development phase

## Current Status

**Current Phase**: Phase 1 - Network Communication
**Status**: Documentation Complete, Implementation Not Started

See [DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md) for detailed progress tracking.

## Contributing

This is currently a personal project. Contributions, suggestions, and feedback are welcome once the initial implementation is complete.

## License

To be determined. Code reuses Venus protocol from Mesa (MIT licensed).

## Acknowledgments

- **Mesa Venus**: For the Venus protocol and architectural inspiration
- **Vulkan Working Group**: For the Vulkan specification
- **VTest**: For demonstrating Venus protocol over sockets

## Contact

Project maintained by: Ayman
Project location: `/home/ayman/venus-plus/`

## See Also

- [Venus Protocol](https://gitlab.freedesktop.org/virgl/venus-protocol)
- [Mesa Venus Driver](https://docs.mesa3d.org/drivers/venus.html)
- [Vulkan Specification](https://www.khronos.org/vulkan/)
