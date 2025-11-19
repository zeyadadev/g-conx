#include "query_state.h"

namespace venus_plus {

QueryState g_query_state;

void QueryState::add_query_pool(VkDevice device,
                                VkQueryPool local,
                                VkQueryPool remote,
                                VkQueryType type,
                                uint32_t query_count,
                                VkQueryPipelineStatisticFlags pipeline_statistics) {
    std::lock_guard<std::mutex> lock(mutex_);
    QueryPoolInfo info;
    info.device = device;
    info.remote_handle = remote;
    info.type = type;
    info.query_count = query_count;
    info.pipeline_statistics = pipeline_statistics;
    query_pools_[handle_key(local)] = info;
}

void QueryState::remove_query_pool(VkQueryPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    query_pools_.erase(handle_key(pool));
}

VkQueryPool QueryState::get_remote_query_pool(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDevice QueryState::get_query_pool_device(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.device;
}

VkQueryType QueryState::get_query_pool_type(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_QUERY_TYPE_MAX_ENUM;
    }
    return it->second.type;
}

uint32_t QueryState::get_query_pool_count(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return 0;
    }
    return it->second.query_count;
}

bool QueryState::validate_query_range(VkQueryPool pool,
                                      uint32_t first_query,
                                      uint32_t query_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return false;
    }
    if (query_count == 0) {
        return true;
    }
    if (first_query >= it->second.query_count) {
        return false;
    }
    uint64_t end = static_cast<uint64_t>(first_query) + query_count;
    return end <= it->second.query_count;
}

void QueryState::remove_device(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = query_pools_.begin(); it != query_pools_.end();) {
        if (it->second.device == device) {
            it = query_pools_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace venus_plus
