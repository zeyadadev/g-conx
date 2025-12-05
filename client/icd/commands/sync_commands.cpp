// Sync Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "profiling.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>

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

struct AccumulatedSubmit {
    VkSubmitInfo submit{};
    SubmitStorage storage;
};

struct SubmitAccumulator {
    VkQueue queue = VK_NULL_HANDLE;
    VkQueue remote_queue = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::vector<AccumulatedSubmit> pending;
};

SubmitAccumulator g_submit_accumulator;

inline bool batch_submit_enabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_BATCH_SUBMITS");
        return env && env[0] != '0';
    }();
    return enabled;
}

inline uint32_t batch_submit_limit() {
    static const uint32_t limit = []() -> uint32_t {
        const char* env = std::getenv("VENUS_BATCH_SUBMITS");
        if (!env || env[0] == '\0') {
            return 8; // default batch size when enabled
        }
        char* end = nullptr;
        long parsed = std::strtol(env, &end, 10);
        if (end == env || parsed <= 0) {
            return 8;
        }
        return static_cast<uint32_t>(parsed);
    }();
    return limit > 0 ? limit : 1;
}

inline void reset_submit_accumulator() {
    g_submit_accumulator = {};
}

inline void fix_submit_pointers(VkSubmitInfo& submit, SubmitStorage& storage) {
    submit.pWaitSemaphores = storage.wait_remote.empty() ? nullptr : storage.wait_remote.data();
    submit.pWaitDstStageMask = storage.wait_stages.empty() ? nullptr : storage.wait_stages.data();
    submit.pCommandBuffers = storage.remote_cbs.empty() ? nullptr : storage.remote_cbs.data();
    submit.pSignalSemaphores = storage.signal_remote.empty() ? nullptr : storage.signal_remote.data();
    if (storage.has_timeline) {
        storage.timeline_info.pWaitSemaphoreValues =
            storage.wait_values.empty() ? nullptr : storage.wait_values.data();
        storage.timeline_info.pSignalSemaphoreValues =
            storage.signal_values.empty() ? nullptr : storage.signal_values.data();
        submit.pNext = &storage.timeline_info;
    } else {
        submit.pNext = nullptr;
    }
}

VkResult flush_submit_accumulator();

inline bool can_batch_submit(const VkSubmitInfo& submit, VkFence fence) {
    if (fence != VK_NULL_HANDLE) {
        return false; // batching would drop fence semantics
    }
    if (submit.waitSemaphoreCount || submit.signalSemaphoreCount) {
        return false; // keep explicit sync out of the batch
    }
    if (submit.pNext && find_timeline_submit_info(submit.pNext)) {
        return false; // timeline values must be honored immediately
    }
    if (submit.pNext) {
        return false; // conservative: skip unknown pNext chains
    }
    return submit.commandBufferCount > 0;
}

VkResult flush_submit_accumulator() {
    if (g_submit_accumulator.pending.empty()) {
        return VK_SUCCESS;
    }

    VkResult flush_result = flush_host_coherent_mappings(g_submit_accumulator.device);
    if (flush_result != VK_SUCCESS) {
        reset_submit_accumulator();
        return flush_result;
    }

    std::vector<VkSubmitInfo> remote_submits;
    remote_submits.reserve(g_submit_accumulator.pending.size());
    for (auto& pending : g_submit_accumulator.pending) {
        fix_submit_pointers(pending.submit, pending.storage);
        remote_submits.push_back(pending.submit);
    }

    const auto submit_start = std::chrono::steady_clock::now();
    VkResult result = vn_call_vkQueueSubmit(&g_ring,
                                            g_submit_accumulator.remote_queue,
                                            static_cast<uint32_t>(remote_submits.size()),
                                            remote_submits.data(),
                                            VK_NULL_HANDLE);
    const auto submit_end = std::chrono::steady_clock::now();
    const uint64_t submit_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(submit_end - submit_start).count());

    if (result == VK_SUCCESS) {
        record_sync_timing(g_submit_timing, submit_us, "vkQueueSubmit(batched)");
    }

    reset_submit_accumulator();
    return result;
}

} // namespace

VkResult venus_flush_submit_accumulator() {
    return flush_submit_accumulator();
}

extern "C" {

// Vulkan function implementations

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(
    VkQueue queue,
    uint32_t bindInfoCount,
    const VkBindSparseInfo* pBindInfo,
    VkFence fence) {

    ICD_LOG_INFO() << "[Client ICD] vkQueueBindSparse called (bindInfoCount=" << bindInfoCount << ")\n";

    if (bindInfoCount > 0 && !pBindInfo) {
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

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    struct SparseBindStorage {
        VkBindSparseInfo info{};
        std::vector<VkSemaphore> wait_semaphores;
        std::vector<VkSemaphore> signal_semaphores;
        std::vector<VkSparseBufferMemoryBindInfo> buffer_infos;
        std::vector<std::vector<VkSparseMemoryBind>> buffer_binds;
        std::vector<VkSparseImageOpaqueMemoryBindInfo> image_opaque_infos;
        std::vector<std::vector<VkSparseMemoryBind>> opaque_binds;
        std::vector<VkSparseImageMemoryBindInfo> image_infos;
        std::vector<std::vector<VkSparseImageMemoryBind>> image_binds;
        VkTimelineSemaphoreSubmitInfo timeline_info{};
        std::vector<uint64_t> wait_values;
        std::vector<uint64_t> signal_values;
        bool has_timeline = false;
    };

    std::vector<VkBindSparseInfo> remote_infos(bindInfoCount);
    std::vector<SparseBindStorage> storage(bindInfoCount);

    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        const VkBindSparseInfo& src = pBindInfo[i];
        VkBindSparseInfo& dst = remote_infos[i];
        SparseBindStorage& slot = storage[i];
        dst = src;

        if (src.waitSemaphoreCount > 0) {
            if (!src.pWaitSemaphores) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: wait semaphores missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.wait_semaphores.resize(src.waitSemaphoreCount);
            for (uint32_t j = 0; j < src.waitSemaphoreCount; ++j) {
                VkSemaphore local_wait = src.pWaitSemaphores[j];
                if (!g_sync_state.has_semaphore(local_wait)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: wait semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                VkSemaphore remote_wait = g_sync_state.get_remote_semaphore(local_wait);
                if (remote_wait == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.wait_semaphores[j] = remote_wait;
            }
            dst.pWaitSemaphores = slot.wait_semaphores.data();
        } else {
            dst.pWaitSemaphores = nullptr;
        }

        if (src.bufferBindCount > 0) {
            if (!src.pBufferBinds) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: buffer binds missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.buffer_infos.resize(src.bufferBindCount);
            slot.buffer_binds.resize(src.bufferBindCount);
            for (uint32_t j = 0; j < src.bufferBindCount; ++j) {
                const VkSparseBufferMemoryBindInfo& buf = src.pBufferBinds[j];
                VkSparseBufferMemoryBindInfo& dst_buf = slot.buffer_infos[j];
                dst_buf = buf;
                dst_buf.buffer = g_resource_state.get_remote_buffer(buf.buffer);
                if (dst_buf.buffer == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: buffer not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (buf.bindCount > 0) {
                    if (!buf.pBinds) {
                        ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: buffer binds array missing\n";
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    slot.buffer_binds[j].assign(buf.pBinds, buf.pBinds + buf.bindCount);
                    for (auto& bind : slot.buffer_binds[j]) {
                        if (bind.memory != VK_NULL_HANDLE) {
                            bind.memory = g_resource_state.get_remote_memory(bind.memory);
                            if (bind.memory == VK_NULL_HANDLE) {
                                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: memory not tracked for buffer bind\n";
                                return VK_ERROR_INITIALIZATION_FAILED;
                            }
                        }
                    }
                    dst_buf.pBinds = slot.buffer_binds[j].data();
                } else {
                    dst_buf.pBinds = nullptr;
                }
            }
            dst.pBufferBinds = slot.buffer_infos.data();
        } else {
            dst.pBufferBinds = nullptr;
        }

        if (src.imageOpaqueBindCount > 0) {
            if (!src.pImageOpaqueBinds) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: image opaque binds missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.image_opaque_infos.resize(src.imageOpaqueBindCount);
            slot.opaque_binds.resize(src.imageOpaqueBindCount);
            for (uint32_t j = 0; j < src.imageOpaqueBindCount; ++j) {
                const VkSparseImageOpaqueMemoryBindInfo& info = src.pImageOpaqueBinds[j];
                VkSparseImageOpaqueMemoryBindInfo& dst_info = slot.image_opaque_infos[j];
                dst_info = info;
                dst_info.image = g_resource_state.get_remote_image(info.image);
                if (dst_info.image == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: image not tracked (opaque)\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (info.bindCount > 0) {
                    if (!info.pBinds) {
                        ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: opaque bind array missing\n";
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    slot.opaque_binds[j].assign(info.pBinds, info.pBinds + info.bindCount);
                    for (auto& bind : slot.opaque_binds[j]) {
                        if (bind.memory != VK_NULL_HANDLE) {
                            bind.memory = g_resource_state.get_remote_memory(bind.memory);
                            if (bind.memory == VK_NULL_HANDLE) {
                                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: memory not tracked for opaque bind\n";
                                return VK_ERROR_INITIALIZATION_FAILED;
                            }
                        }
                    }
                    dst_info.pBinds = slot.opaque_binds[j].data();
                } else {
                    dst_info.pBinds = nullptr;
                }
            }
            dst.pImageOpaqueBinds = slot.image_opaque_infos.data();
        } else {
            dst.pImageOpaqueBinds = nullptr;
        }

        if (src.imageBindCount > 0) {
            if (!src.pImageBinds) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: image binds missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.image_infos.resize(src.imageBindCount);
            slot.image_binds.resize(src.imageBindCount);
            for (uint32_t j = 0; j < src.imageBindCount; ++j) {
                const VkSparseImageMemoryBindInfo& info = src.pImageBinds[j];
                VkSparseImageMemoryBindInfo& dst_info = slot.image_infos[j];
                dst_info = info;
                dst_info.image = g_resource_state.get_remote_image(info.image);
                if (dst_info.image == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: image not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (info.bindCount > 0) {
                    if (!info.pBinds) {
                        ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: image bind array missing\n";
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    slot.image_binds[j].assign(info.pBinds, info.pBinds + info.bindCount);
                    for (auto& bind : slot.image_binds[j]) {
                        if (bind.memory != VK_NULL_HANDLE) {
                            bind.memory = g_resource_state.get_remote_memory(bind.memory);
                            if (bind.memory == VK_NULL_HANDLE) {
                                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: memory not tracked for image bind\n";
                                return VK_ERROR_INITIALIZATION_FAILED;
                            }
                        }
                    }
                    dst_info.pBinds = slot.image_binds[j].data();
                } else {
                    dst_info.pBinds = nullptr;
                }
            }
            dst.pImageBinds = slot.image_infos.data();
        } else {
            dst.pImageBinds = nullptr;
        }

        if (src.signalSemaphoreCount > 0) {
            if (!src.pSignalSemaphores) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: signal semaphores missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.signal_semaphores.resize(src.signalSemaphoreCount);
            for (uint32_t j = 0; j < src.signalSemaphoreCount; ++j) {
                VkSemaphore local_signal = src.pSignalSemaphores[j];
                if (!g_sync_state.has_semaphore(local_signal)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse: signal semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                VkSemaphore remote_signal = g_sync_state.get_remote_semaphore(local_signal);
                if (remote_signal == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.signal_semaphores[j] = remote_signal;
            }
            dst.pSignalSemaphores = slot.signal_semaphores.data();
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

    IcdQueue* icd_queue = icd_queue_from_handle(queue);
    VkDevice queue_device = icd_queue ? icd_queue->parent_device : VK_NULL_HANDLE;
    VkResult flush_result = flush_host_coherent_mappings(queue_device);
    if (flush_result != VK_SUCCESS) {
        return flush_result;
    }

    VkResult result =
        vn_call_vkQueueBindSparse(&g_ring, remote_queue, bindInfoCount, remote_infos.data(), remote_fence);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueBindSparse failed: " << result << "\n";
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        const SparseBindStorage& slot = storage[i];
        for (size_t j = 0; j < slot.wait_semaphores.size(); ++j) {
            VkSemaphore sem = pBindInfo[i].pWaitSemaphores ? pBindInfo[i].pWaitSemaphores[j] : VK_NULL_HANDLE;
            if (sem != VK_NULL_HANDLE && g_sync_state.get_semaphore_type(sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(sem, false);
            } else if (slot.has_timeline && j < slot.wait_values.size() && sem != VK_NULL_HANDLE) {
                g_sync_state.set_timeline_value(sem, slot.wait_values[j]);
            }
        }
        for (size_t j = 0; j < slot.signal_semaphores.size(); ++j) {
            VkSemaphore sem = pBindInfo[i].pSignalSemaphores ? pBindInfo[i].pSignalSemaphores[j] : VK_NULL_HANDLE;
            if (sem != VK_NULL_HANDLE && g_sync_state.get_semaphore_type(sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(sem, true);
            } else if (slot.has_timeline && j < slot.signal_values.size() && sem != VK_NULL_HANDLE) {
                g_sync_state.set_timeline_value(sem, slot.signal_values[j]);
            }
        }
    }

    ICD_LOG_INFO() << "[Client ICD] vkQueueBindSparse completed\n";
    return VK_SUCCESS;
}

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
    const auto wait_start = std::chrono::steady_clock::now();
    VkResult result = vn_call_vkWaitForFences(&g_ring,
                                              icd_device->remote_handle,
                                              fenceCount,
                                              remote_fences.data(),
                                              waitAll,
                                              timeout);
    const auto wait_end = std::chrono::steady_clock::now();
    const uint64_t elapsed_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count());
    if (result != VK_SUCCESS) {
        return result;
    }
    for (uint32_t i = 0; i < fenceCount; ++i) {
        g_sync_state.set_fence_signaled(pFences[i], true);
    }
    VkResult invalidate_result = invalidate_host_coherent_mappings(device);
    if (invalidate_result != VK_SUCCESS) {
        return invalidate_result;
    }
    record_sync_timing(g_wait_timing, elapsed_us, "vkWaitForFences");
    return VK_SUCCESS;
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

    if (batch_submit_enabled() && !g_submit_accumulator.pending.empty()) {
        VkResult flush_result = flush_submit_accumulator();
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
    }

    IcdQueue* icd_queue = icd_queue_from_handle(queue);
    VkDevice queue_device = icd_queue ? icd_queue->parent_device : VK_NULL_HANDLE;

    const bool batching = batch_submit_enabled();
    if (batching && !g_submit_accumulator.pending.empty() && g_submit_accumulator.queue != queue) {
        VkResult flush_result = flush_submit_accumulator();
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
    }

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

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

    bool all_batchable = batching && submitCount > 0;
    if (all_batchable) {
        for (uint32_t i = 0; i < submitCount; ++i) {
            if (!can_batch_submit(pSubmits[i], fence)) {
                all_batchable = false;
                break;
            }
        }
    }

    if (all_batchable) {
        if (g_submit_accumulator.pending.empty()) {
            g_submit_accumulator.queue = queue;
            g_submit_accumulator.remote_queue = remote_queue;
            g_submit_accumulator.device = queue_device;
        }

        if (g_submit_accumulator.queue != queue) {
            VkResult flush_result = flush_submit_accumulator();
            if (flush_result != VK_SUCCESS) {
                return flush_result;
            }
            g_submit_accumulator.queue = queue;
            g_submit_accumulator.remote_queue = remote_queue;
            g_submit_accumulator.device = queue_device;
        }

        const uint32_t limit = batch_submit_limit();
        if (g_submit_accumulator.pending.size() + submitCount > limit) {
            VkResult flush_result = flush_submit_accumulator();
            if (flush_result != VK_SUCCESS) {
                return flush_result;
            }
            g_submit_accumulator.queue = queue;
            g_submit_accumulator.remote_queue = remote_queue;
            g_submit_accumulator.device = queue_device;
        }

        for (uint32_t i = 0; i < submitCount; ++i) {
            AccumulatedSubmit acc;
            acc.storage = std::move(storage[i]);
            acc.submit = remote_submits[i];
            fix_submit_pointers(acc.submit, acc.storage);
            g_submit_accumulator.pending.push_back(std::move(acc));
        }

        ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit batched (pending="
                  << g_submit_accumulator.pending.size() << ")\n";
        return VK_SUCCESS;
    }

    if (batching && !g_submit_accumulator.pending.empty()) {
        VkResult flush_result = flush_submit_accumulator();
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
    }

    VkResult flush_result = flush_host_coherent_mappings(queue_device);
    if (flush_result != VK_SUCCESS) {
        return flush_result;
    }

    const VkSubmitInfo* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    const auto submit_start = std::chrono::steady_clock::now();
    VkResult result = vn_call_vkQueueSubmit(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);

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

    VkResult flush_result = flush_host_coherent_mappings(queue_device);
    if (flush_result != VK_SUCCESS) {
        return flush_result;
    }

    const VkSubmitInfo2* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    const auto submit_start = std::chrono::steady_clock::now();
    VkResult result = vn_call_vkQueueSubmit2(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);

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

    if (batch_submit_enabled() && !g_submit_accumulator.pending.empty()) {
        VkResult flush_result = flush_submit_accumulator();
        if (flush_result != VK_SUCCESS) {
            return flush_result;
        }
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
