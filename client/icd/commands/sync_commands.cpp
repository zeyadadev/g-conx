// Sync Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "profiling.h"
#include <atomic>
#include <chrono>

namespace {

struct TimingState {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> total_us{0};
    std::atomic<uint64_t> max_us{0};
};

inline void record_sync_timing(TimingState& state, uint64_t elapsed_us, const char* tag) {
    const uint64_t count = state.calls.fetch_add(1, std::memory_order_relaxed) + 1;
    state.total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    uint64_t prev_max = state.max_us.load(std::memory_order_relaxed);
    while (elapsed_us > prev_max &&
           !state.max_us.compare_exchange_weak(prev_max, elapsed_us, std::memory_order_relaxed)) {
    }
    if (memory_trace_enabled() && (count % 100 == 0)) {
        const uint64_t total = state.total_us.load(std::memory_order_relaxed);
        const uint64_t max_seen = state.max_us.load(std::memory_order_relaxed);
        const double avg_us = count ? static_cast<double>(total) / static_cast<double>(count) : 0.0;
        VP_LOG_STREAM_INFO(MEMORY) << "[Sync] " << tag << " summary: calls=" << count
                                   << " avg_us=" << avg_us
                                   << " max_us=" << max_seen;
    }
}

TimingState g_submit_timing;
TimingState g_wait_timing;

} // namespace

extern "C" {

// Vulkan function implementations

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(
    VkDevice device,
    const VkEventCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkEvent* pEvent) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateEvent called\n";

    if (!pCreateInfo || !pEvent) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkEvent remote_event = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateEvent(&g_ring,
                                            icd_device->remote_handle,
                                            pCreateInfo,
                                            pAllocator,
                                            &remote_event);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateEvent failed: " << result << "\n";
        return result;
    }

    VkEvent local_event = g_handle_allocator.allocate<VkEvent>();
    g_sync_state.add_event(device, local_event, remote_event, false);
    *pEvent = local_event;
    ICD_LOG_INFO() << "[Client ICD] Event created (local=" << local_event << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice device,
    VkEvent event,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyEvent called\n";

    if (event == VK_NULL_HANDLE) {
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    g_sync_state.remove_event(event);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyEvent\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyEvent\n";
        return;
    }

    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkDestroyEvent\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyEvent(&g_ring, icd_device->remote_handle, remote_event, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkGetEventStatus called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetEventStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkGetEventStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetEventStatus(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_EVENT_SET) {
        g_sync_state.set_event_signaled(event, true);
    } else if (result == VK_EVENT_RESET) {
        g_sync_state.set_event_signaled(event, false);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkSetEvent called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkSetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkSetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkSetEvent(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_SUCCESS) {
        g_sync_state.set_event_signaled(event, true);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkResetEvent called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkResetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetEvent(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_SUCCESS) {
        g_sync_state.set_event_signaled(event, false);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFence* pFence) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateFence called\n";

    if (!pCreateInfo || !pFence) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkFence remote_fence = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateFence(&g_ring, icd_device->remote_handle, pCreateInfo, pAllocator, &remote_fence);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateFence failed: " << result << "\n";
        return result;
    }

    VkFence local_fence = g_handle_allocator.allocate<VkFence>();
    g_sync_state.add_fence(device, local_fence, remote_fence, (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
    *pFence = local_fence;
    ICD_LOG_INFO() << "[Client ICD] Fence created (local=" << *pFence << ", remote=" << remote_fence << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyFence called\n";
    if (fence == VK_NULL_HANDLE) {
        return;
    }

    VkFence remote = g_sync_state.get_remote_fence(fence);
    g_sync_state.remove_fence(fence);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyFence\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyFence\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote fence missing in vkDestroyFence\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyFence(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence) {
    ICD_LOG_INFO() << "[Client ICD] vkGetFenceStatus called\n";
    if (!g_sync_state.has_fence(fence)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown fence in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkFence remote = g_sync_state.get_remote_fence(fence);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    const auto wait_start = std::chrono::steady_clock::now();
    VkResult result = vn_call_vkGetFenceStatus(&g_ring, icd_device->remote_handle, remote);
    const auto wait_end = std::chrono::steady_clock::now();
    if (result == VK_SUCCESS) {
        g_sync_state.set_fence_signaled(fence, true);
        VkResult invalidate_result = invalidate_host_coherent_mappings(device);
        if (invalidate_result != VK_SUCCESS) {
            return invalidate_result;
        }
        const uint64_t elapsed_us =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count());
        record_sync_timing(g_wait_timing, elapsed_us, "vkGetFenceStatus");
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences) {

    ICD_LOG_INFO() << "[Client ICD] vkResetFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkResetFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetFences(&g_ring,
                                            icd_device->remote_handle,
                                            fenceCount,
                                            remote_fences.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < fenceCount; ++i) {
            g_sync_state.set_fence_signaled(pFences[i], false);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout) {

    VENUS_PROFILE_WAIT_FENCES();

    // Heuristic token detection: assume each wait after the initial warmup is a token
    // This is approximate but gives useful metrics
    static std::atomic<uint64_t> wait_count{0};
    static constexpr uint64_t WARMUP_WAITS = 50; // Skip warmup phase
    uint64_t current_wait = wait_count.fetch_add(1, std::memory_order_relaxed);

    if (current_wait >= WARMUP_WAITS) {
        VENUS_PROFILE_TOKEN();
    }

    // Print profiling summary every 10 seconds
    static auto last_print = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print).count();
    if (elapsed >= 10) {
        VENUS_PROFILE_PRINT();
        last_print = now;
    }

    ICD_LOG_INFO() << "[Client ICD] vkWaitForFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkWaitForFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkWaitForFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    const bool enable_latency = latency_mode_enabled();
    const bool trace_mem = memory_trace_enabled();
    InvalidateBatchPayload invalidate_batch;
    if (enable_latency) {
        VkResult prep = build_invalidate_payload(device, &invalidate_batch, trace_mem);
        if (prep != VK_SUCCESS) {
            return prep;
        }
    }

    const auto wait_start = std::chrono::steady_clock::now();
    VkResult result = VK_SUCCESS;
    uint64_t elapsed_us = 0;

    if (!enable_latency) {
        result = vn_call_vkWaitForFences(&g_ring,
                                         icd_device->remote_handle,
                                         fenceCount,
                                         remote_fences.data(),
                                         waitAll,
                                         timeout);
        const auto wait_end = std::chrono::steady_clock::now();
        elapsed_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count());
        if (result == VK_SUCCESS) {
            for (uint32_t i = 0; i < fenceCount; ++i) {
                g_sync_state.set_fence_signaled(pFences[i], true);
            }
            VkResult invalidate_result = invalidate_host_coherent_mappings(device);
            if (invalidate_result != VK_SUCCESS) {
                return invalidate_result;
            }
            record_sync_timing(g_wait_timing, elapsed_us, "vkWaitForFences");
        }
        return result;
    }

    size_t cmd_size = vn_sizeof_vkWaitForFences(icd_device->remote_handle,
                                               fenceCount,
                                               remote_fences.data(),
                                               waitAll,
                                               timeout);
    if (!cmd_size || cmd_size > std::numeric_limits<uint32_t>::max()) {
        finalize_invalidate_ranges(invalidate_batch);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    WaitInvalidateHeader header = {};
    header.command = VENUS_PLUS_CMD_COALESCE_WAIT;
    header.flags = kVenusCoalesceFlagCommand;
    header.wait_command_size = static_cast<uint32_t>(cmd_size);
    header.invalidate_size = static_cast<uint32_t>(invalidate_batch.request.size());
    if (!invalidate_batch.request.empty()) {
        header.flags |= kVenusCoalesceFlagInvalidate;
    }

    const size_t payload_size = sizeof(header) + cmd_size + invalidate_batch.request.size();
    if (!check_payload_size(payload_size)) {
        finalize_invalidate_ranges(invalidate_batch);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    std::vector<uint8_t> payload(payload_size);
    std::memcpy(payload.data(), &header, sizeof(header));

    std::vector<uint8_t> command_stream(cmd_size);
    vn_cs_encoder encoder;
    vn_cs_encoder_init_external(&encoder, command_stream.data(), cmd_size);
    vn_encode_vkWaitForFences(&encoder,
                              VK_COMMAND_GENERATE_REPLY_BIT_EXT,
                              icd_device->remote_handle,
                              fenceCount,
                              remote_fences.data(),
                              waitAll,
                              timeout);

    size_t offset = sizeof(header);
    std::memcpy(payload.data() + offset, command_stream.data(), command_stream.size());
    offset += command_stream.size();
    if (!invalidate_batch.request.empty()) {
        std::memcpy(payload.data() + offset,
                    invalidate_batch.request.data(),
                    invalidate_batch.request.size());
    }

    if (!g_client.send(payload.data(), payload.size())) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send coalesced wait-for-fences";
        finalize_invalidate_ranges(invalidate_batch);
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply) || reply.size() < sizeof(WaitInvalidateReplyHeader)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive coalesced wait reply";
        finalize_invalidate_ranges(invalidate_batch);
        return VK_ERROR_DEVICE_LOST;
    }

    WaitInvalidateReplyHeader reply_header = {};
    std::memcpy(&reply_header, reply.data(), sizeof(reply_header));
    const size_t expected_size = sizeof(reply_header) +
                                 reply_header.wait_reply_size +
                                 reply_header.invalidate_reply_size;
    if (reply.size() != expected_size) {
        ICD_LOG_ERROR() << "[Client ICD] Coalesced wait reply size mismatch";
        finalize_invalidate_ranges(invalidate_batch);
        return VK_ERROR_DEVICE_LOST;
    }

    vn_cs_decoder decoder;
    vn_cs_decoder_init(&decoder,
                       reply.data() + sizeof(reply_header),
                       reply_header.wait_reply_size);
    result = vn_decode_vkWaitForFences_reply(&decoder,
                                             icd_device->remote_handle,
                                             fenceCount,
                                             remote_fences.data(),
                                             waitAll,
                                             timeout);
    vn_cs_decoder_reset_temp_storage(&decoder);

    const auto wait_end = std::chrono::steady_clock::now();
    elapsed_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count());

    if (result != VK_SUCCESS) {
        finalize_invalidate_ranges(invalidate_batch);
        return result;
    }

    for (uint32_t i = 0; i < fenceCount; ++i) {
        g_sync_state.set_fence_signaled(pFences[i], true);
    }
    if (!invalidate_batch.request.empty()) {
        std::vector<uint8_t> invalidate_reply(reply_header.invalidate_reply_size);
        if (reply_header.invalidate_reply_size > 0) {
            std::memcpy(invalidate_reply.data(),
                        reply.data() + sizeof(reply_header) + reply_header.wait_reply_size,
                        reply_header.invalidate_reply_size);
        }
        VkResult invalidate_result =
            apply_invalidate_reply(invalidate_batch, invalidate_reply, elapsed_us, trace_mem);
        if (invalidate_result != VK_SUCCESS) {
            return invalidate_result;
        }
    }
    record_sync_timing(g_wait_timing, elapsed_us, "vkWaitForFences");
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateSemaphore called\n";

    if (!pCreateInfo || !pSemaphore) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSemaphore remote_semaphore = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateSemaphore(&g_ring,
                                                icd_device->remote_handle,
                                                pCreateInfo,
                                                pAllocator,
                                                &remote_semaphore);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateSemaphore failed: " << result << "\n";
        return result;
    }

    const VkSemaphoreTypeCreateInfo* type_info = find_semaphore_type_info(pCreateInfo);
    VkSemaphoreType type = type_info ? type_info->semaphoreType : VK_SEMAPHORE_TYPE_BINARY;
    uint64_t initial_value = type_info ? type_info->initialValue : 0;

    VkSemaphore local_semaphore = g_handle_allocator.allocate<VkSemaphore>();
    g_sync_state.add_semaphore(device,
                               local_semaphore,
                               remote_semaphore,
                               type,
                               false,
                               initial_value);
    *pSemaphore = local_semaphore;
    ICD_LOG_INFO() << "[Client ICD] Semaphore created (local=" << *pSemaphore
              << ", remote=" << remote_semaphore
              << ", type=" << (type == VK_SEMAPHORE_TYPE_TIMELINE ? "timeline" : "binary") << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroySemaphore called\n";
    if (semaphore == VK_NULL_HANDLE) {
        return;
    }

    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    g_sync_state.remove_semaphore(semaphore);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroySemaphore\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroySemaphore\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote semaphore missing in vkDestroySemaphore\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroySemaphore(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {

    VENUS_PROFILE_QUEUE_SUBMIT();

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit called (submitCount=" << submitCount << ")\n";

    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdQueue* icd_queue = icd_queue_from_handle(queue);
    VkDevice queue_device = icd_queue ? icd_queue->parent_device : VK_NULL_HANDLE;

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    struct SubmitStorage {
        std::vector<VkSemaphore> wait_local;
        std::vector<VkSemaphore> signal_local;
        std::vector<VkSemaphore> wait_remote;
        std::vector<VkPipelineStageFlags> wait_stages;
        std::vector<VkCommandBuffer> remote_cbs;
        std::vector<VkSemaphore> signal_remote;
        std::vector<uint64_t> wait_values;
        std::vector<uint64_t> signal_values;
        VkTimelineSemaphoreSubmitInfo timeline_info{};
        bool has_timeline = false;
    };

    std::vector<VkSubmitInfo> remote_submits;
    std::vector<SubmitStorage> storage;
    if (submitCount > 0) {
        remote_submits.resize(submitCount);
        storage.resize(submitCount);
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo& src = pSubmits[i];
        VkSubmitInfo& dst = remote_submits[i];
        SubmitStorage& slot = storage[i];
        dst = src;

        if (src.waitSemaphoreCount > 0) {
            if (!src.pWaitSemaphores || !src.pWaitDstStageMask) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: invalid wait semaphore arrays\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.wait_local.assign(src.pWaitSemaphores, src.pWaitSemaphores + src.waitSemaphoreCount);
            slot.wait_remote.resize(src.waitSemaphoreCount);
            slot.wait_stages.assign(src.pWaitDstStageMask, src.pWaitDstStageMask + src.waitSemaphoreCount);
            for (uint32_t j = 0; j < src.waitSemaphoreCount; ++j) {
                VkSemaphore wait_sem = src.pWaitSemaphores[j];
                if (!g_sync_state.has_semaphore(wait_sem)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: wait semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.wait_remote[j] = g_sync_state.get_remote_semaphore(wait_sem);
                if (slot.wait_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pWaitSemaphores = slot.wait_remote.data();
            dst.pWaitDstStageMask = slot.wait_stages.data();
        } else {
            dst.pWaitSemaphores = nullptr;
            dst.pWaitDstStageMask = nullptr;
        }

        if (src.commandBufferCount > 0) {
            if (!src.pCommandBuffers) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffers missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.remote_cbs.resize(src.commandBufferCount);
            for (uint32_t j = 0; j < src.commandBufferCount; ++j) {
                VkCommandBuffer local_cb = src.pCommandBuffers[j];
                if (!g_command_buffer_state.has_command_buffer(local_cb)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffer not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (g_command_buffer_state.get_buffer_state(local_cb) != CommandBufferLifecycleState::EXECUTABLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffer not executable\n";
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }
                VkCommandBuffer remote_cb = get_remote_command_buffer_handle(local_cb);
                if (remote_cb == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.remote_cbs[j] = remote_cb;
            }
            dst.pCommandBuffers = slot.remote_cbs.data();
        } else {
            dst.pCommandBuffers = nullptr;
        }

        if (src.signalSemaphoreCount > 0) {
            if (!src.pSignalSemaphores) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: signal semaphores missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.signal_local.assign(src.pSignalSemaphores, src.pSignalSemaphores + src.signalSemaphoreCount);
            slot.signal_remote.resize(src.signalSemaphoreCount);
            for (uint32_t j = 0; j < src.signalSemaphoreCount; ++j) {
                VkSemaphore signal_sem = src.pSignalSemaphores[j];
                if (!g_sync_state.has_semaphore(signal_sem)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: signal semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.signal_remote[j] = g_sync_state.get_remote_semaphore(signal_sem);
                if (slot.signal_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pSignalSemaphores = slot.signal_remote.data();
        } else {
            dst.pSignalSemaphores = nullptr;
        }

        const VkTimelineSemaphoreSubmitInfo* timeline = find_timeline_submit_info(src.pNext);
        if (timeline) {
            slot.timeline_info = *timeline;
            if (timeline->waitSemaphoreValueCount) {
                slot.wait_values.assign(timeline->pWaitSemaphoreValues,
                                        timeline->pWaitSemaphoreValues + timeline->waitSemaphoreValueCount);
                slot.timeline_info.pWaitSemaphoreValues = slot.wait_values.data();
            }
            if (timeline->signalSemaphoreValueCount) {
                slot.signal_values.assign(timeline->pSignalSemaphoreValues,
                                          timeline->pSignalSemaphoreValues + timeline->signalSemaphoreValueCount);
                slot.timeline_info.pSignalSemaphoreValues = slot.signal_values.data();
            }
            dst.pNext = &slot.timeline_info;
            slot.has_timeline = true;
        } else {
            dst.pNext = nullptr;
            slot.has_timeline = false;
        }
    }

    const bool enable_latency = latency_mode_enabled();
    FlushBatchPayload flush_batch;
    if (enable_latency) {
        VkResult flush_prep = build_flush_payload(queue_device, &flush_batch);
        if (flush_prep != VK_SUCCESS) {
            return flush_prep;
        }
    } else {
        VkResult flush_result = flush_host_coherent_mappings(queue_device);
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
    }

    const VkSubmitInfo* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    const auto submit_start = std::chrono::steady_clock::now();
    VkResult result = VK_SUCCESS;

    if (!enable_latency) {
        result = vn_call_vkQueueSubmit(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);
    } else {
        size_t cmd_size = vn_sizeof_vkQueueSubmit(remote_queue, submitCount, submit_ptr, remote_fence);
        if (!cmd_size || cmd_size > std::numeric_limits<uint32_t>::max()) {
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        std::vector<uint8_t> command_stream(cmd_size);
        vn_cs_encoder encoder;
        vn_cs_encoder_init_external(&encoder, command_stream.data(), cmd_size);
        vn_encode_vkQueueSubmit(&encoder,
                                VK_COMMAND_GENERATE_REPLY_BIT_EXT,
                                remote_queue,
                                submitCount,
                                submit_ptr,
                                remote_fence);

        SubmitCoalesceHeader header = {};
        header.command = VENUS_PLUS_CMD_COALESCE_SUBMIT;
        header.flags = kVenusCoalesceFlagCommand;
        header.transfer_size = static_cast<uint32_t>(flush_batch.payload.size());
        header.command_size = static_cast<uint32_t>(command_stream.size());
        if (!flush_batch.payload.empty()) {
            header.flags |= kVenusCoalesceFlagTransfer;
        }

        const size_t payload_size = sizeof(header) + flush_batch.payload.size() + command_stream.size();
        if (!check_payload_size(payload_size)) {
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        std::vector<uint8_t> payload(payload_size);
        std::memcpy(payload.data(), &header, sizeof(header));
        size_t offset = sizeof(header);
        if (!flush_batch.payload.empty()) {
            std::memcpy(payload.data() + offset, flush_batch.payload.data(), flush_batch.payload.size());
            offset += flush_batch.payload.size();
        }
        std::memcpy(payload.data() + offset, command_stream.data(), command_stream.size());

        if (!g_client.send(payload.data(), payload.size())) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to send coalesced submit";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        std::vector<uint8_t> reply;
        if (!g_client.receive(reply) || reply.size() < sizeof(SubmitCoalesceReplyHeader)) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to receive coalesced submit reply";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        SubmitCoalesceReplyHeader reply_header = {};
        std::memcpy(&reply_header, reply.data(), sizeof(reply_header));
        if (reply_header.transfer_result != VK_SUCCESS) {
            finalize_flush_ranges(flush_batch);
            return reply_header.transfer_result;
        }

        const size_t expected_reply_size = sizeof(reply_header) + reply_header.command_reply_size;
        if (reply.size() != expected_reply_size) {
            ICD_LOG_ERROR() << "[Client ICD] Coalesced submit reply size mismatch";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        vn_cs_decoder decoder;
        vn_cs_decoder_init(&decoder,
                           reply.data() + sizeof(reply_header),
                           reply_header.command_reply_size);
        result = vn_decode_vkQueueSubmit_reply(&decoder, remote_queue, submitCount, submit_ptr, remote_fence);
        vn_cs_decoder_reset_temp_storage(&decoder);
        finalize_flush_ranges(flush_batch);
    }

    const auto submit_end = std::chrono::steady_clock::now();
    const uint64_t submit_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(submit_end - submit_start).count());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit failed: " << result << "\n";
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    for (uint32_t i = 0; i < submitCount; ++i) {
        const SubmitStorage& slot = storage[i];
        for (VkSemaphore wait_sem : slot.wait_local) {
            if (g_sync_state.get_semaphore_type(wait_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(wait_sem, false);
            }
        }
        if (slot.has_timeline && !slot.wait_values.empty()) {
            for (size_t j = 0; j < slot.wait_local.size() && j < slot.wait_values.size(); ++j) {
                if (g_sync_state.get_semaphore_type(slot.wait_local[j]) == VK_SEMAPHORE_TYPE_TIMELINE) {
                    g_sync_state.set_timeline_value(slot.wait_local[j], slot.wait_values[j]);
                }
            }
        }
        for (size_t j = 0; j < slot.signal_local.size(); ++j) {
            VkSemaphore signal_sem = slot.signal_local[j];
            if (g_sync_state.get_semaphore_type(signal_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(signal_sem, true);
            } else if (slot.has_timeline && j < slot.signal_values.size()) {
                g_sync_state.set_timeline_value(signal_sem, slot.signal_values[j]);
            }
        }
    }

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit completed\n";
    record_sync_timing(g_submit_timing, submit_us, "vkQueueSubmit");
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2* pSubmits,
    VkFence fence) {

    VENUS_PROFILE_QUEUE_SUBMIT();

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit2 called (submitCount=" << submitCount << ")\n";

    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdQueue* icd_queue = icd_queue_from_handle(queue);
    VkDevice queue_device = icd_queue ? icd_queue->parent_device : VK_NULL_HANDLE;

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    struct Submit2Storage {
        std::vector<VkSemaphoreSubmitInfo> wait_infos;
        std::vector<VkCommandBufferSubmitInfo> command_infos;
        std::vector<VkSemaphoreSubmitInfo> signal_infos;
        std::vector<VkSemaphore> wait_local;
        std::vector<VkSemaphore> signal_local;
    };

    std::vector<VkSubmitInfo2> remote_submits(submitCount);
    std::vector<Submit2Storage> storage(submitCount);

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo2& src = pSubmits[i];
        VkSubmitInfo2& dst = remote_submits[i];
        Submit2Storage& slot = storage[i];
        dst = src;

        if (src.waitSemaphoreInfoCount > 0) {
            if (!src.pWaitSemaphoreInfos) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: wait semaphore infos missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.wait_infos.assign(src.pWaitSemaphoreInfos,
                                   src.pWaitSemaphoreInfos + src.waitSemaphoreInfoCount);
            slot.wait_local.reserve(slot.wait_infos.size());
            for (auto& info : slot.wait_infos) {
                if (!g_sync_state.has_semaphore(info.semaphore)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: wait semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.wait_local.push_back(info.semaphore);
                VkSemaphore remote_sem = g_sync_state.get_remote_semaphore(info.semaphore);
                if (remote_sem == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                info.semaphore = remote_sem;
            }
            dst.pWaitSemaphoreInfos = slot.wait_infos.data();
        } else {
            dst.pWaitSemaphoreInfos = nullptr;
        }

        if (src.commandBufferInfoCount > 0) {
            if (!src.pCommandBufferInfos) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: command buffer infos missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.command_infos.assign(src.pCommandBufferInfos,
                                      src.pCommandBufferInfos + src.commandBufferInfoCount);
            for (auto& info : slot.command_infos) {
                VkCommandBuffer local_cb = info.commandBuffer;
                if (!g_command_buffer_state.has_command_buffer(local_cb)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: command buffer not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (g_command_buffer_state.get_buffer_state(local_cb) != CommandBufferLifecycleState::EXECUTABLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: command buffer not executable\n";
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }
                VkCommandBuffer remote_cb = get_remote_command_buffer_handle(local_cb);
                if (remote_cb == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                info.commandBuffer = remote_cb;
            }
            dst.pCommandBufferInfos = slot.command_infos.data();
        } else {
            dst.pCommandBufferInfos = nullptr;
        }

        if (src.signalSemaphoreInfoCount > 0) {
            if (!src.pSignalSemaphoreInfos) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: signal semaphore infos missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.signal_infos.assign(src.pSignalSemaphoreInfos,
                                     src.pSignalSemaphoreInfos + src.signalSemaphoreInfoCount);
            slot.signal_local.reserve(slot.signal_infos.size());
            for (auto& info : slot.signal_infos) {
                if (!g_sync_state.has_semaphore(info.semaphore)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2: signal semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.signal_local.push_back(info.semaphore);
                VkSemaphore remote_sem = g_sync_state.get_remote_semaphore(info.semaphore);
                if (remote_sem == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                info.semaphore = remote_sem;
            }
            dst.pSignalSemaphoreInfos = slot.signal_infos.data();
        } else {
            dst.pSignalSemaphoreInfos = nullptr;
        }
    }

    const bool enable_latency = latency_mode_enabled();
    FlushBatchPayload flush_batch;
    if (enable_latency) {
        VkResult flush_prep = build_flush_payload(queue_device, &flush_batch);
        if (flush_prep != VK_SUCCESS) {
            return flush_prep;
        }
    } else {
        VkResult flush_result = flush_host_coherent_mappings(queue_device);
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
    }

    const VkSubmitInfo2* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    const auto submit_start = std::chrono::steady_clock::now();
    VkResult result = VK_SUCCESS;

    if (!enable_latency) {
        result = vn_call_vkQueueSubmit2(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);
    } else {
        size_t cmd_size = vn_sizeof_vkQueueSubmit2(remote_queue, submitCount, submit_ptr, remote_fence);
        if (!cmd_size || cmd_size > std::numeric_limits<uint32_t>::max()) {
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        std::vector<uint8_t> command_stream(cmd_size);
        vn_cs_encoder encoder;
        vn_cs_encoder_init_external(&encoder, command_stream.data(), cmd_size);
        vn_encode_vkQueueSubmit2(&encoder,
                                 VK_COMMAND_GENERATE_REPLY_BIT_EXT,
                                 remote_queue,
                                 submitCount,
                                 submit_ptr,
                                 remote_fence);

        SubmitCoalesceHeader header = {};
        header.command = VENUS_PLUS_CMD_COALESCE_SUBMIT;
        header.flags = kVenusCoalesceFlagCommand;
        header.transfer_size = static_cast<uint32_t>(flush_batch.payload.size());
        header.command_size = static_cast<uint32_t>(command_stream.size());
        if (!flush_batch.payload.empty()) {
            header.flags |= kVenusCoalesceFlagTransfer;
        }

        const size_t payload_size = sizeof(header) + flush_batch.payload.size() + command_stream.size();
        if (!check_payload_size(payload_size)) {
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        std::vector<uint8_t> payload(payload_size);
        std::memcpy(payload.data(), &header, sizeof(header));
        size_t offset = sizeof(header);
        if (!flush_batch.payload.empty()) {
            std::memcpy(payload.data() + offset, flush_batch.payload.data(), flush_batch.payload.size());
            offset += flush_batch.payload.size();
        }
        std::memcpy(payload.data() + offset, command_stream.data(), command_stream.size());

        if (!g_client.send(payload.data(), payload.size())) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to send coalesced submit2";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        std::vector<uint8_t> reply;
        if (!g_client.receive(reply) || reply.size() < sizeof(SubmitCoalesceReplyHeader)) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to receive coalesced submit2 reply";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        SubmitCoalesceReplyHeader reply_header = {};
        std::memcpy(&reply_header, reply.data(), sizeof(reply_header));
        if (reply_header.transfer_result != VK_SUCCESS) {
            finalize_flush_ranges(flush_batch);
            return reply_header.transfer_result;
        }

        const size_t expected_reply = sizeof(reply_header) + reply_header.command_reply_size;
        if (reply.size() != expected_reply) {
            ICD_LOG_ERROR() << "[Client ICD] Coalesced submit2 reply size mismatch";
            finalize_flush_ranges(flush_batch);
            return VK_ERROR_DEVICE_LOST;
        }

        vn_cs_decoder decoder;
        vn_cs_decoder_init(&decoder,
                           reply.data() + sizeof(reply_header),
                           reply_header.command_reply_size);
        result = vn_decode_vkQueueSubmit2_reply(&decoder, remote_queue, submitCount, submit_ptr, remote_fence);
        vn_cs_decoder_reset_temp_storage(&decoder);
        finalize_flush_ranges(flush_batch);
    }

    const auto submit_end = std::chrono::steady_clock::now();
    const uint64_t submit_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(submit_end - submit_start).count());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit2 failed: " << result << "\n";
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        g_sync_state.set_fence_signaled(fence, true);
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const Submit2Storage& slot = storage[i];
        for (size_t j = 0; j < slot.wait_local.size(); ++j) {
            VkSemaphore sem = slot.wait_local[j];
            VkSemaphoreType type = g_sync_state.get_semaphore_type(sem);
            if (type == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(sem, false);
            } else if (type == VK_SEMAPHORE_TYPE_TIMELINE && j < slot.wait_infos.size()) {
                g_sync_state.set_timeline_value(sem, slot.wait_infos[j].value);
            }
        }
        for (size_t j = 0; j < slot.signal_local.size(); ++j) {
            VkSemaphore sem = slot.signal_local[j];
            VkSemaphoreType type = g_sync_state.get_semaphore_type(sem);
            if (type == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(sem, true);
            } else if (type == VK_SEMAPHORE_TYPE_TIMELINE && j < slot.signal_infos.size()) {
                g_sync_state.set_timeline_value(sem, slot.signal_infos[j].value);
            }
        }
    }

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit2 completed\n";
    record_sync_timing(g_submit_timing, submit_us, "vkQueueSubmit2");
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2* pSubmits,
    VkFence fence) {
    return vkQueueSubmit2(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    ICD_LOG_INFO() << "[Client ICD] vkQueueWaitIdle called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdQueue* icd_queue = icd_queue_from_handle(queue);
    VkDevice queue_device = icd_queue ? icd_queue->parent_device : VK_NULL_HANDLE;
    VkResult result = vn_call_vkQueueWaitIdle(&g_ring, remote_queue);
    if (result == VK_SUCCESS) {
        VkResult invalidate_result = invalidate_host_coherent_mappings(queue_device);
        if (invalidate_result != VK_SUCCESS) {
            return invalidate_result;
        }
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueWaitIdle failed: " << result << "\n";
    }
    return result;
}

} // extern "C"
