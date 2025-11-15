# Phase 08: Memory Data Transfer

**Transfer buffer/image data over network**

## Overview

**Goal**: Implement memory mapping and data transfer between client and server.

**Duration**: 11 days (Days 50-60)

**Prerequisites**: Phase 07 complete and passing

## Objectives

- ✅ Client can map memory (creates shadow buffer)
- ✅ Client transfers data to server on unmap
- ✅ Server writes received data to real GPU memory
- ✅ Client can read back data from server
- ✅ Support for large data transfers
- ✅ Test app writes data, GPU processes, reads back result

**🎉 MILESTONE**: Complete data transfer pipeline!

## Commands to Implement

### Memory Mapping
- [ ] `vkMapMemory`
- [ ] `vkUnmapMemory`
- [ ] `vkFlushMappedMemoryRanges`
- [ ] `vkInvalidateMappedMemoryRanges`

### Custom Protocol Extensions
- [ ] `VENUS_PLUS_TRANSFER_MEMORY_DATA` - Client → Server
- [ ] `VENUS_PLUS_READ_MEMORY_DATA` - Server → Client

## New Components

### Client Components

**1. Shadow Buffer Manager** (`client/state/shadow_buffer.h/cpp`)
- Allocate shadow buffers for mapped memory
- Track mapped ranges
- Store modified data
- Free shadow buffers on unmap

### Server Components

**1. Memory Transfer Handler** (`server/memory/memory_transfer.h/cpp`)
- Receive memory data from client
- Write to real GPU memory
- Read from GPU memory
- Support partial updates

## Memory Transfer Protocol

### Custom Command IDs

```cpp
enum VenusPlusCommandType {
    VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA = 0x10000000,
    VENUS_PLUS_CMD_READ_MEMORY_DATA     = 0x10000001,
};
```

### TRANSFER_MEMORY_DATA Message Format

```
Payload:
  [Command Type: uint32_t] = TRANSFER_MEMORY_DATA
  [VkDeviceMemory: uint64_t] - Client memory handle
  [Offset: uint64_t] - Offset in memory
  [Size: uint64_t] - Data size in bytes
  [Data: uint8_t[]] - Actual data (variable length)
```

### READ_MEMORY_DATA Message Format

```
Request:
  [Command Type: uint32_t] = READ_MEMORY_DATA
  [VkDeviceMemory: uint64_t] - Client memory handle
  [Offset: uint64_t] - Offset to read from
  [Size: uint64_t] - Bytes to read

Reply:
  [VkResult: uint32_t] - Success/failure
  [Data: uint8_t[]] - Requested data
```

## Detailed Requirements

### vkMapMemory

**Client Behavior**:
- [ ] Verify memory handle is valid
- [ ] Verify memory was allocated with HOST_VISIBLE flag
- [ ] Allocate shadow buffer (malloc or mmap)
- [ ] If memory already has data on server:
  - Send READ_MEMORY_DATA request
  - Receive data from server
  - Initialize shadow buffer with received data
- [ ] Return pointer to shadow buffer
- [ ] Store mapping info (memory handle, offset, size, pointer)

**Server Behavior**:
- [ ] For READ_MEMORY_DATA request:
  - Translate memory handle to real memory
  - Map real memory: `vkMapMemory(real_device, real_memory, offset, size, 0, &ptr)`
  - Read data from mapped memory
  - Unmap: `vkUnmapMemory(real_device, real_memory)`
  - Send data back to client

**Note**: Server doesn't need to keep memory mapped - only map temporarily to read/write.

### vkUnmapMemory

**Client Behavior**:
- [ ] Verify memory is currently mapped
- [ ] Get shadow buffer pointer and size
- [ ] Send TRANSFER_MEMORY_DATA message:
  - Include memory handle, offset, size
  - Append shadow buffer data
- [ ] Receive acknowledgment from server
- [ ] Free shadow buffer
- [ ] Remove mapping info

**Server Behavior**:
- [ ] Receive TRANSFER_MEMORY_DATA message
- [ ] Decode memory handle, offset, size, data
- [ ] Translate memory handle to real memory
- [ ] Map real memory: `vkMapMemory(...)`
- [ ] Copy received data to mapped memory: `memcpy(mapped_ptr + offset, received_data, size)`
- [ ] Unmap: `vkUnmapMemory(...)`
- [ ] Send acknowledgment

### vkFlushMappedMemoryRanges

**Client Behavior**:
- [ ] For non-coherent memory
- [ ] Send TRANSFER_MEMORY_DATA for specified ranges
- [ ] Server flushes: `vkFlushMappedMemoryRanges(...)`

### vkInvalidateMappedMemoryRanges

**Client Behavior**:
- [ ] For non-coherent memory
- [ ] Send READ_MEMORY_DATA for specified ranges
- [ ] Update shadow buffer with received data

## Shadow Buffer Management

### Shadow Buffer Lifecycle

```
vkMapMemory:
  1. Allocate shadow buffer (size bytes)
  2. Optional: Read current GPU data into shadow
  3. Return pointer to app

App writes to shadow buffer

vkUnmapMemory:
  1. Transfer shadow buffer → server
  2. Server writes to real GPU memory
  3. Free shadow buffer
```

### Shadow Buffer Structure

```cpp
struct ShadowBuffer {
    VkDeviceMemory memory;        // Client memory handle
    VkDeviceSize offset;          // Mapped offset
    VkDeviceSize size;            // Mapped size
    void* data;                   // Shadow buffer (malloc'd)
    bool modified;                // Needs transfer on unmap
};
```

## Large Data Transfer Optimization

### Chunked Transfer (Optional)

For very large transfers (> 1MB):
- [ ] Split into chunks (e.g., 256KB each)
- [ ] Send multiple TRANSFER_MEMORY_DATA messages
- [ ] Server reassembles data

### Compression (Future Enhancement)

- Use zlib/lz4 to compress data before transfer
- Significant savings for textures, uniform buffers

## Test Application

**File**: `test-app/phase08/phase08_test.cpp`

**Test Steps**:
1. [ ] Create device, queue
2. [ ] Create staging buffer (1MB, host visible)
3. [ ] Allocate memory (host visible)
4. [ ] Bind buffer to memory
5. [ ] Map memory
6. [ ] Write test pattern (0x12345678 repeated)
7. [ ] Unmap memory (triggers transfer)
8. [ ] Create device buffer (device local)
9. [ ] Allocate memory
10. [ ] Bind buffer
11. [ ] Record command: copy staging → device
12. [ ] Submit and wait
13. [ ] Map staging buffer again
14. [ ] Verify data (read back pattern)
15. [ ] Cleanup

**Expected Output**:
```
Phase 8: Memory Data Transfer
==============================
✅ Allocated 1MB staging buffer (host visible)
✅ Mapped memory
✅ Wrote test data (0x12345678 pattern, 262144 iterations)
✅ Unmapping... transferring 1048576 bytes to server
✅ Transfer complete (took 15ms, 70MB/s)
✅ Created device buffer
✅ Recorded copy command
✅ Submitted to GPU
✅ GPU copy complete
✅ Reading back data...
✅ Read 1048576 bytes from server (took 12ms)
✅ Data verification: PASSED
✅ All 262144 uint32_t values correct
✅ Phase 8 PASSED

🎉 MILESTONE: Data transfer working!
```

## Implementation Checklist

### Days 1-3: Shadow Buffer Implementation
- [ ] Implement shadow buffer manager
- [ ] Implement vkMapMemory (client)
- [ ] Test shadow buffer allocation

### Days 4-6: Memory Transfer Protocol
- [ ] Define protocol messages
- [ ] Implement TRANSFER_MEMORY_DATA (client → server)
- [ ] Implement vkUnmapMemory (client)
- [ ] Implement server memory transfer handler
- [ ] Test data transfer

### Days 7-9: Read-Back Support
- [ ] Implement READ_MEMORY_DATA (server → client)
- [ ] Test read-back
- [ ] Test round-trip (write, GPU process, read)

### Days 10-11: Testing & Optimization
- [ ] Implement phase 8 test app
- [ ] Test large transfers (10MB+)
- [ ] Measure transfer performance
- [ ] Optimize if needed
- [ ] Regression tests

## Success Criteria

- [ ] Memory mapping works correctly
- [ ] Data transfers to server
- [ ] Server writes to real GPU memory
- [ ] Data can be read back
- [ ] Large transfers work (10MB+)
- [ ] Phase 8 test passes
- [ ] Regression tests pass
- [ ] Performance acceptable (> 50MB/s on localhost)

## Deliverables

- [ ] Shadow buffer manager
- [ ] Memory transfer protocol implementation
- [ ] vkMapMemory implementation
- [ ] vkUnmapMemory implementation
- [ ] Read-back support
- [ ] Server memory transfer handler
- [ ] Phase 8 test application
- [ ] Performance measurements

## Performance Benchmarks

**Target Performance** (localhost):
- Small transfers (< 4KB): < 1ms
- Medium transfers (1MB): 10-20ms
- Large transfers (10MB): 100-200ms
- Throughput: > 50MB/s

## Common Issues & Solutions

**Issue**: Transfer times out for large data
**Solution**: Increase network timeout, or implement chunking

**Issue**: Data corruption on transfer
**Solution**: Verify endianness, check memcpy boundaries

**Issue**: Memory not HOST_VISIBLE
**Solution**: Validate memory type flags before mapping

**Issue**: Shadow buffer not freed
**Solution**: Ensure unmap always frees shadow buffer

## Next Steps

Proceed to **[PHASE_09.md](PHASE_09.md)** for compute shader support!
