# Venus Plus Build and Run Guide

**Complete instructions for building and running Venus Plus**

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Initial Setup](#initial-setup)
3. [Building](#building)
4. [Running](#running)
5. [Development Workflow](#development-workflow)
6. [Troubleshooting](#troubleshooting)

## Prerequisites

### System Requirements

- **Operating System**: Linux (Ubuntu 20.04+, Fedora 34+, or equivalent)
- **CPU**: x86_64 architecture
- **RAM**: Minimum 4GB
- **Disk Space**: ~2GB for build artifacts

### Required Software

**Build Tools**:
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config

# Fedora/RHEL
sudo dnf install -y \
    gcc \
    g++ \
    cmake \
    git \
    pkg-config
```

**C++ Compiler**:
- GCC 9.0+ or Clang 10.0+
- C++17 support required

**Vulkan SDK**:
```bash
# Ubuntu/Debian
sudo apt-get install -y \
    libvulkan-dev \
    vulkan-validationlayers \
    vulkan-tools

# Fedora/RHEL
sudo dnf install -y \
    vulkan-headers \
    vulkan-loader-devel \
    vulkan-validation-layers \
    vulkan-tools

# Verify installation
vulkaninfo | head -20
```

**Optional but Recommended**:
```bash
# Debugging tools
sudo apt-get install -y \
    gdb \
    valgrind \
    strace

# Testing framework
sudo apt-get install -y \
    libgtest-dev

# Build Google Test (Ubuntu)
cd /usr/src/gtest
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib
```

### Verify Prerequisites

```bash
# Check C++ compiler
g++ --version
# Should show GCC 9.0 or higher

# Check CMake
cmake --version
# Should show CMake 3.20 or higher

# Check Vulkan
vulkaninfo --summary
# Should show available GPUs

# Check if you have a GPU
lspci | grep -i vga
# Should show your graphics card
```

## Initial Setup

### 1. Clone Repository (Future)

```bash
# When repository is available
git clone https://github.com/yourusername/venus-plus.git
cd venus-plus
```

### 2. Copy Venus Protocol Headers

The Venus protocol headers need to be copied from Mesa:

```bash
# Copy from your Mesa build
mkdir -p common/venus-protocol
cp /home/ayman/mesa/src/virtio/venus-protocol/*.h common/venus-protocol/

# Create README documenting the source
cat > common/venus-protocol/README.md << 'EOF'
# Venus Protocol Headers

**Source**: Mesa 3D Graphics Library
**Location**: https://gitlab.freedesktop.org/mesa/mesa
**Path**: src/virtio/venus-protocol/
**Version**: git-7157163d (as of 2025-11-15)

These headers are auto-generated from the venus-protocol project:
https://gitlab.freedesktop.org/virgl/venus-protocol

## Updating

To update these headers:
1. Pull latest Mesa
2. Copy src/virtio/venus-protocol/*.h to this directory
3. Update version info above
EOF
```

### 3. Project Structure

Ensure your directory structure matches the documented layout:

```bash
cd /home/ayman/venus-plus
ls -la

# Expected structure:
# .
# ├── README.md
# ├── CMakeLists.txt (to be created)
# ├── docs/
# ├── common/
# ├── client/
# ├── server/
# └── test-app/
```

## Building

### Quick Build

```bash
cd /home/ayman/venus-plus

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build (use all CPU cores)
make -j$(nproc)

# Expected output:
# [ 10%] Building CXX object common/CMakeFiles/venus_common.dir/...
# [ 20%] Building CXX object client/CMakeFiles/venus_client.dir/...
# [ 50%] Building CXX object server/CMakeFiles/venus_server.dir/...
# [100%] Building CXX object test-app/CMakeFiles/venus_test_app.dir/...
# [100%] Built target venus_test_app
```

### Build Options

**Debug Build** (default):
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

**Release Build** (optimized):
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Build with Tests**:
```bash
cmake -DBUILD_TESTING=ON ..
make -j$(nproc)
```

**Build Specific Target**:
```bash
# Build only client
make venus_client

# Build only server
make venus_server

# Build only test app
make venus_test_app
```

**Verbose Build** (see full commands):
```bash
make VERBOSE=1
```

### Build Output

After successful build:

```
build/
├── client/
│   ├── libvenus_icd.so           # ICD shared library
│   └── venus_icd.x86_64.json     # ICD manifest
├── server/
│   └── venus-server              # Server executable
├── test-app/
│   └── venus-test-app            # Test application
└── common/
    └── libvenus_common.a         # Common library
```

### Clean Build

```bash
# Clean build artifacts
cd build
make clean

# Complete rebuild
cd ..
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Running

### Basic Workflow

You need **two terminals**:
1. **Terminal 1**: Run the server
2. **Terminal 2**: Run the client application

### Terminal 1: Start Server

```bash
cd /home/ayman/venus-plus/build

# Start server on default port (5556)
./server/venus-server

# Output:
# Venus Plus Server v0.1
# Listening on 0.0.0.0:5556
# Using GPU: NVIDIA GeForce RTX 3080
# Waiting for clients...
```

**Server Options**:
```bash
# Specify port
./server/venus-server --port 5000

# Enable debug logging
./server/venus-server --log-level DEBUG

# Enable Vulkan validation layers
./server/venus-server --validation

# Bind to specific interface
./server/venus-server --bind 192.168.1.100

# Show help
./server/venus-server --help
```

### Terminal 2: Run Test Application

```bash
cd /home/ayman/venus-plus/build

# Set ICD environment variable
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json

# Run test app
./test-app/venus-test-app --phase 1

# Output:
# Venus Plus Test Application
# =================================================
# Phase 1: Network Communication
# =================================================
# Connecting to server at 127.0.0.1:5556...
# ✅ Connected
# Testing vkEnumerateInstanceVersion...
# ✅ vkEnumerateInstanceVersion returned: 1.3.0
# ✅ PASSED
```

**Test App Options**:
```bash
# Run specific phase
./test-app/venus-test-app --phase 3

# Run all phases up to N
./test-app/venus-test-app --up-to 5

# Run all phases
./test-app/venus-test-app --all

# Connect to custom server
./test-app/venus-test-app --server 192.168.1.100:5000 --phase 1

# Enable verbose output
./test-app/venus-test-app --verbose --phase 2

# Show help
./test-app/venus-test-app --help
```

### Running Tests

**Unit Tests**:
```bash
cd build

# Run all unit tests
ctest --output-on-failure

# Run specific test
./client/tests/test_venus_encoder
./server/tests/test_decoder

# Run with verbose output
ctest -V
```

**Phase Tests** (see above)

### Using with Existing Vulkan Applications

Once the ICD is working, you can use it with any Vulkan application:

```bash
cd /home/ayman/venus-plus/build

# Terminal 1: Start server
./server/venus-server

# Terminal 2: Run your Vulkan app
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json

# Run any Vulkan application
vulkaninfo
vkcube
your-vulkan-game
```

**Example with vkcube**:
```bash
# Install vkcube if not present
sudo apt-get install vulkan-tools

# Run with Venus Plus
export VK_DRIVER_FILES=/home/ayman/venus-plus/build/client/venus_icd.x86_64.json
vkcube

# You should see:
# - vkcube window opens
# - Rendering happens on remote GPU
# - Image transferred back and displayed
```

## Development Workflow

### Typical Development Cycle

1. **Write Code**
   ```bash
   vim client/commands/instance.cpp
   ```

2. **Build**
   ```bash
   cd build
   make -j$(nproc)
   ```

3. **Run Tests**
   ```bash
   # Terminal 1
   ./server/venus-server &

   # Terminal 2
   export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
   ./test-app/venus-test-app --phase 2
   ```

4. **Debug if Needed**
   ```bash
   # Run server with GDB
   gdb --args ./server/venus-server
   (gdb) run

   # In another terminal, run client
   export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
   ./test-app/venus-test-app --phase 2

   # Server will stop at breakpoints
   ```

5. **Iterate**

### Fast Rebuild

```bash
# Only rebuild changed files
cd build
make -j$(nproc)

# If CMake files changed
cmake ..
make -j$(nproc)
```

### Development Build Script

Create a helper script:

```bash
# scripts/dev-build.sh
#!/bin/bash
set -e

cd /home/ayman/venus-plus/build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON ..
make -j$(nproc)

echo ""
echo "✅ Build successful!"
echo ""
echo "To run:"
echo "  Terminal 1: ./server/venus-server"
echo "  Terminal 2: VK_DRIVER_FILES=\$(pwd)/client/venus_icd.x86_64.json ./test-app/venus-test-app --phase N"
```

```bash
chmod +x scripts/dev-build.sh
./scripts/dev-build.sh
```

### Running Server in Background

```bash
cd build

# Start server in background
./server/venus-server > server.log 2>&1 &
SERVER_PID=$!
sleep 1  # Wait for server to start

# Run tests
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
./test-app/venus-test-app --all

# Kill server
kill $SERVER_PID
```

## Troubleshooting

### Build Issues

**Issue**: `CMake Error: Could not find CMAKE_C_COMPILER`

**Solution**:
```bash
sudo apt-get install build-essential
```

---

**Issue**: `fatal error: vulkan/vulkan.h: No such file or directory`

**Solution**:
```bash
sudo apt-get install libvulkan-dev
```

---

**Issue**: `undefined reference to vn_encode_vkCreateInstance`

**Solution**: Venus protocol headers not copied correctly
```bash
cp /home/ayman/mesa/src/virtio/venus-protocol/*.h common/venus-protocol/
```

---

**Issue**: `CMake Error: The source directory does not contain a CMakeLists.txt`

**Solution**: Run cmake from build directory
```bash
cd /home/ayman/venus-plus
mkdir build
cd build
cmake ..
```

### Runtime Issues

**Issue**: Client can't connect to server

```
Error: Connection refused (111)
```

**Solution**:
1. Check server is running: `ps aux | grep venus-server`
2. Check port: `netstat -tuln | grep 5556`
3. Check firewall: `sudo ufw allow 5556`

---

**Issue**: `VK_ERROR_INCOMPATIBLE_DRIVER`

**Solution**: ICD manifest incorrect
```bash
# Check ICD manifest exists
ls -la build/client/venus_icd.x86_64.json

# Check ICD manifest points to correct .so
cat build/client/venus_icd.x86_64.json

# Check .so exists
ls -la build/client/libvenus_icd.so

# Re-set environment variable
export VK_DRIVER_FILES=$(realpath build/client/venus_icd.x86_64.json)
```

---

**Issue**: Server crashes with segmentation fault

**Solution**: Run with GDB
```bash
gdb --args ./server/venus-server
(gdb) run
# When it crashes:
(gdb) bt
(gdb) info locals
```

---

**Issue**: Server reports validation errors

**Solution**: Enable validation layers to see details
```bash
./server/venus-server --validation

# Fix the reported Vulkan usage errors
```

---

**Issue**: Memory leaks reported by Valgrind

**Solution**: Run specific test with Valgrind
```bash
valgrind --leak-check=full --show-leak-kinds=all \
./test-app/venus-test-app --phase 3

# Fix reported leaks in code
```

### Performance Issues

**Issue**: Slow network communication

**Solution**:
1. Check if running over network vs localhost
2. Enable TCP_NODELAY (disable Nagle's algorithm)
3. Increase buffer sizes

---

**Issue**: High latency

**Solution**:
1. Use localhost (127.0.0.1) for testing
2. Check network stats: `iperf3 -s` / `iperf3 -c <server>`
3. Reduce logging verbosity

### Getting Help

1. **Check logs**:
   ```bash
   # Server log
   ./server/venus-server --log-level DEBUG

   # Client log
   export VENUS_CLIENT_LOG_LEVEL=DEBUG
   ```

2. **Check documentation**:
   - [ARCHITECTURE.md](ARCHITECTURE.md)
   - [TESTING_STRATEGY.md](TESTING_STRATEGY.md)
   - Phase-specific docs: [PHASE_01.md](PHASE_01.md), etc.

3. **Verify setup**:
   ```bash
   # Run verification script
   ./scripts/verify-setup.sh
   ```

---

**Next**: Start development with [PHASE_01.md](PHASE_01.md)!
