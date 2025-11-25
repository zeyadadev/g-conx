#include "shadow_buffer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include "utils/logging.h"

namespace venus_plus {

namespace {

constexpr size_t kShadowBufferAlignment = 64;

inline size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline size_t system_page_size() {
    static const size_t kPageSize = []() -> size_t {
        long ps = sysconf(_SC_PAGESIZE);
        return ps > 0 ? static_cast<size_t>(ps) : 4096;
    }();
    return kPageSize;
}

struct FaultRegionNode;

struct FaultRegionNode {
    void* base = nullptr;
    size_t size = 0;
    HostCoherentTracking* tracking = nullptr;
    std::atomic<bool> active{true};
    FaultRegionNode* next = nullptr;
};

std::atomic<FaultRegionNode*> g_fault_regions{nullptr};
std::once_flag g_fault_handler_once;
struct sigaction g_prev_segv = {};
bool g_fault_handler_installed = false;
bool g_fault_handler_ready = false;

void shadow_fault_handler(int sig, siginfo_t* info, void* uctx);

void install_shadow_fault_handler() {
    std::call_once(g_fault_handler_once, []() {
        struct sigaction sa = {};
        sa.sa_sigaction = shadow_fault_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        if (sigaction(SIGSEGV, &sa, &g_prev_segv) == 0) {
            g_fault_handler_installed = true;
            g_fault_handler_ready = true;
        }
    });
}

void register_fault_region(HostCoherentTracking* tracking) {
    if (!tracking) {
        return;
    }
    install_shadow_fault_handler();
    auto* node = new FaultRegionNode();
    node->base = tracking->base;
    node->size = tracking->alloc_size;
    node->tracking = tracking;
    FaultRegionNode* head = g_fault_regions.load(std::memory_order_acquire);
    do {
        node->next = head;
    } while (!g_fault_regions.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_relaxed));
    tracking->fault_node = node;
}

void unregister_fault_region(HostCoherentTracking* tracking) {
    auto* node = tracking ? static_cast<FaultRegionNode*>(tracking->fault_node) : nullptr;
    if (!node) {
        return;
    }
    node->active.store(false, std::memory_order_release);
    tracking->fault_node = nullptr;
}

void make_pages_writable(HostCoherentTracking* tracking, size_t page_index) {
    if (!tracking) {
        return;
    }
    const size_t page_size = tracking->page_size;
    void* page_addr = static_cast<uint8_t*>(tracking->base) + page_index * page_size;
    mprotect(page_addr, page_size, PROT_READ | PROT_WRITE);
}

void make_pages_readonly(HostCoherentTracking* tracking, size_t first_page, size_t page_count) {
    if (!tracking) {
        return;
    }
    const size_t page_size = tracking->page_size;
    size_t current = first_page;
    const size_t end_page = std::min(first_page + page_count, tracking->page_count);
    while (current < end_page) {
        if (tracking->writable[current].load(std::memory_order_relaxed) == 0) {
            ++current;
            continue;
        }
        size_t run_start = current;
        while (current < end_page && tracking->writable[current].load(std::memory_order_relaxed) == 1) {
            tracking->writable[current].store(0, std::memory_order_relaxed);
            ++current;
        }
        size_t pages = current - run_start;
        void* addr = static_cast<uint8_t*>(tracking->base) + run_start * page_size;
        mprotect(addr, pages * page_size, PROT_READ);
    }
}

void shadow_fault_handler(int sig, siginfo_t* info, void* uctx) {
    void* fault_addr = info ? info->si_addr : nullptr;
    FaultRegionNode* node = g_fault_regions.load(std::memory_order_acquire);
    while (node) {
        if (node->active.load(std::memory_order_acquire)) {
            uint8_t* start = static_cast<uint8_t*>(node->base);
            uint8_t* end = start + node->size;
            if (fault_addr >= start && fault_addr < end) {
                HostCoherentTracking* tracking = node->tracking;
                size_t offset = static_cast<uint8_t*>(fault_addr) - start;
                size_t page = offset / tracking->page_size;
                if (page < tracking->page_count) {
                    tracking->dirty[page].store(1, std::memory_order_relaxed);
                    if (tracking->writable[page].exchange(1, std::memory_order_relaxed) == 0) {
                        make_pages_writable(tracking, page);
                    }
                    return;
                }
            }
        }
        node = node->next;
    }

    if (g_fault_handler_installed) {
        if (g_prev_segv.sa_flags & SA_SIGINFO) {
            g_prev_segv.sa_sigaction(sig, info, uctx);
        } else if (g_prev_segv.sa_handler == SIG_IGN) {
            return;
        } else if (g_prev_segv.sa_handler == SIG_DFL) {
            signal(SIGSEGV, SIG_DFL);
            raise(SIGSEGV);
        } else {
            g_prev_segv.sa_handler(sig);
        }
    } else {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    }
}

void clear_dirty_bits(HostCoherentTracking* tracking, size_t first_page, size_t page_count) {
    if (!tracking) {
        return;
    }
    const size_t end_page = std::min(first_page + page_count, tracking->page_count);
    for (size_t i = first_page; i < end_page; ++i) {
        tracking->dirty[i].store(0, std::memory_order_relaxed);
    }
}

void destroy_host_coherent_tracking(ShadowBufferMapping& mapping) {
    HostCoherentTracking* tracking = mapping.tracking;
    if (!tracking) {
        if (mapping.data) {
            std::free(mapping.data);
        }
        mapping.data = nullptr;
        return;
    }

    unregister_fault_region(tracking);
    if (mapping.data && mapping.alloc_size) {
        munmap(mapping.data, mapping.alloc_size);
    }
    delete tracking;
    mapping.tracking = nullptr;
    mapping.data = nullptr;
    mapping.alloc_size = 0;
}

} // namespace

ShadowBufferManager g_shadow_buffer_manager;

ShadowBufferManager::ShadowBufferManager() = default;

ShadowBufferManager::~ShadowBufferManager() {
    free_all_locked();
}

bool ShadowBufferManager::create_mapping(VkDevice device,
                                         VkDeviceMemory memory,
                                         VkDeviceSize offset,
                                         VkDeviceSize size,
                                         bool host_coherent,
                                         bool invalidate_on_wait,
                                         void** out_ptr) {
    if (!out_ptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (mappings_.count(handle_key(memory)) != 0) {
        return false;
    }

    ShadowBufferMapping mapping = {};
    mapping.device = device;
    mapping.memory = memory;
    mapping.offset = offset;
    mapping.size = size;
    mapping.host_coherent = host_coherent;
    mapping.invalidate_on_wait = invalidate_on_wait;

    bool use_fault_tracking = host_coherent && size > 0;
    if (use_fault_tracking) {
        install_shadow_fault_handler();
        if (!g_fault_handler_ready) {
            use_fault_tracking = false;
        }
    }

    if (use_fault_tracking && size > 0) {
        size_t page_size = system_page_size();
        size_t alloc_size = align_up(static_cast<size_t>(size), page_size);
        if (alloc_size == 0) {
            alloc_size = page_size;
        }
        // Map with write access so the initial shadow copy can be populated.
        // reset_host_coherent_mapping() will flip the pages back to read-only
        // once vkMapMemory finishes seeding the buffer.
        void* ptr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return false;
        }
        auto* tracking = new HostCoherentTracking();
        tracking->base = ptr;
        tracking->size = static_cast<size_t>(size);
        tracking->alloc_size = alloc_size;
        tracking->page_size = page_size;
        tracking->page_count = (alloc_size + page_size - 1) / page_size;
        tracking->dirty.reset(new std::atomic<uint8_t>[tracking->page_count]);
        tracking->writable.reset(new std::atomic<uint8_t>[tracking->page_count]);
        for (size_t i = 0; i < tracking->page_count; ++i) {
            tracking->dirty[i].store(0, std::memory_order_relaxed);
            tracking->writable[i].store(0, std::memory_order_relaxed);
        }
        register_fault_region(tracking);
        mapping.data = ptr;
        mapping.alloc_size = alloc_size;
        mapping.tracking = tracking;
    } else if (size > 0) {
        size_t alloc_size = align_up(static_cast<size_t>(size), kShadowBufferAlignment);
        if (alloc_size == 0) {
            alloc_size = kShadowBufferAlignment;
        }
        void* ptr = nullptr;
        if (posix_memalign(&ptr, kShadowBufferAlignment, alloc_size) != 0) {
            return false;
        }
        std::memset(ptr, 0, alloc_size);
        mapping.data = ptr;
        mapping.alloc_size = alloc_size;
    }

    mappings_[handle_key(memory)] = mapping;
    *out_ptr = mapping.data;
    return true;
}

bool ShadowBufferManager::remove_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(handle_key(memory));
    if (it == mappings_.end()) {
        return false;
    }
    ShadowBufferMapping stored = it->second;
    mappings_.erase(it);
    if (out_mapping) {
        *out_mapping = stored;
    } else {
        destroy_host_coherent_tracking(stored);
    }
    return true;
}

bool ShadowBufferManager::get_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) const {
    if (!out_mapping) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(handle_key(memory));
    if (it == mappings_.end()) {
        return false;
    }
    *out_mapping = it->second;
    return true;
}

bool ShadowBufferManager::is_mapped(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mappings_.find(handle_key(memory)) != mappings_.end();
}

void ShadowBufferManager::collect_dirty_coherent_ranges(VkDevice device,
                                                        std::vector<ShadowCoherentRange>* out_ranges) const {
    if (!out_ranges) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    out_ranges->clear();
    for (const auto& entry : mappings_) {
        const ShadowBufferMapping& mapping = entry.second;
        HostCoherentTracking* tracking = mapping.tracking;
        if (!mapping.host_coherent || !tracking || mapping.device != device || mapping.size == 0) {
            continue;
        }
        const size_t page_size = tracking->page_size;
        const size_t total_bytes = static_cast<size_t>(mapping.size);
        size_t page = 0;
        while (page < tracking->page_count) {
            if (tracking->dirty[page].load(std::memory_order_relaxed) == 0) {
                ++page;
                continue;
            }
            size_t start = page;
            while (page < tracking->page_count &&
                   tracking->dirty[page].load(std::memory_order_relaxed) == 1) {
                ++page;
            }
            size_t page_count = page - start;
            size_t byte_offset = start * page_size;
            if (byte_offset >= total_bytes) {
                break;
            }
            size_t byte_length = page_count * page_size;
            if (byte_offset + byte_length > total_bytes) {
                byte_length = total_bytes - byte_offset;
            }
            ShadowCoherentRange range = {};
            range.memory = mapping.memory;
            range.offset = mapping.offset + byte_offset;
            range.size = byte_length;
            range.data = static_cast<uint8_t*>(mapping.data) + byte_offset;
            range.tracking = mapping.tracking;
            range.first_page = start;
            range.page_count = std::max<size_t>(1, (byte_length + page_size - 1) / page_size);
            out_ranges->push_back(range);
        }
    }
}

void ShadowBufferManager::prepare_coherent_range_flush(const ShadowCoherentRange& range) const {
    make_pages_readonly(range.tracking, range.first_page, range.page_count);
}

void ShadowBufferManager::finalize_coherent_range_flush(const ShadowCoherentRange& range) const {
    clear_dirty_bits(range.tracking, range.first_page, range.page_count);
}

void ShadowBufferManager::collect_host_coherent_ranges(VkDevice device,
                                                       std::vector<ShadowCoherentRange>* out_ranges) const {
    if (!out_ranges) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    out_ranges->clear();
    for (const auto& entry : mappings_) {
        const ShadowBufferMapping& mapping = entry.second;
        if (!mapping.host_coherent || !mapping.data || mapping.size == 0 || mapping.device != device) {
            continue;
        }
        if (!mapping.invalidate_on_wait) {
            continue;
        }
        ShadowCoherentRange range = {};
        range.memory = mapping.memory;
        range.offset = mapping.offset;
        range.size = mapping.size;
        range.data = mapping.data;
        range.tracking = mapping.tracking;
        range.first_page = 0;
        range.page_count = mapping.tracking ? mapping.tracking->page_count : 0;
        out_ranges->push_back(range);
    }
}

bool ShadowBufferManager::range_has_dirty_pages(const ShadowCoherentRange& range) const {
    HostCoherentTracking* tracking = range.tracking;
    if (!tracking) {
        return false;
    }
    const size_t end_page = std::min(range.first_page + range.page_count, tracking->page_count);
    for (size_t i = range.first_page; i < end_page; ++i) {
        if (tracking->dirty[i].load(std::memory_order_relaxed) != 0) {
            return true;
        }
    }
    return false;
}

void ShadowBufferManager::prepare_coherent_range_invalidate(const ShadowCoherentRange& range) const {
    HostCoherentTracking* tracking = range.tracking;
    if (!tracking || range.page_count == 0) {
        return;
    }
    const size_t first_page = std::min(range.first_page, tracking->page_count);
    const size_t page_count = std::min(range.page_count, tracking->page_count - first_page);
    if (page_count == 0) {
        return;
    }
    const size_t page_size = tracking->page_size;
    void* addr = static_cast<uint8_t*>(tracking->base) + first_page * page_size;
    mprotect(addr, page_count * page_size, PROT_READ | PROT_WRITE);
}

void ShadowBufferManager::finalize_coherent_range_invalidate(const ShadowCoherentRange& range) const {
    HostCoherentTracking* tracking = range.tracking;
    if (!tracking || range.page_count == 0) {
        return;
    }
    const size_t first_page = std::min(range.first_page, tracking->page_count);
    const size_t page_count = std::min(range.page_count, tracking->page_count - first_page);
    if (page_count == 0) {
        return;
    }
    const size_t page_size = tracking->page_size;
    void* addr = static_cast<uint8_t*>(tracking->base) + first_page * page_size;
    mprotect(addr, page_count * page_size, PROT_READ);
    for (size_t i = first_page; i < first_page + page_count && i < tracking->page_count; ++i) {
        tracking->writable[i].store(0, std::memory_order_relaxed);
    }
}

void ShadowBufferManager::reset_host_coherent_mapping(VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(handle_key(memory));
    if (it == mappings_.end()) {
        return;
    }
    ShadowBufferMapping& mapping = it->second;
    HostCoherentTracking* tracking = mapping.tracking;
    if (!mapping.host_coherent || !tracking) {
        return;
    }
    mprotect(tracking->base, tracking->alloc_size, PROT_READ);
    for (size_t i = 0; i < tracking->page_count; ++i) {
        tracking->dirty[i].store(0, std::memory_order_relaxed);
        tracking->writable[i].store(0, std::memory_order_relaxed);
    }
}

void ShadowBufferManager::remove_device(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = mappings_.begin(); it != mappings_.end();) {
        if (it->second.device == device) {
            destroy_host_coherent_tracking(it->second);
            it = mappings_.erase(it);
        } else {
            ++it;
        }
    }
}

void ShadowBufferManager::free_all_locked() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : mappings_) {
        destroy_host_coherent_tracking(entry.second);
    }
    mappings_.clear();
}

void ShadowBufferManager::free_mapping_resources(ShadowBufferMapping* mapping) const {
    if (!mapping) {
        return;
    }
    destroy_host_coherent_tracking(*mapping);
    mapping->data = nullptr;
    mapping->tracking = nullptr;
    mapping->alloc_size = 0;
}

} // namespace venus_plus
