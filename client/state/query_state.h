#ifndef VENUS_PLUS_QUERY_STATE_H
#define VENUS_PLUS_QUERY_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>

namespace venus_plus {

struct QueryPoolInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool remote_handle = VK_NULL_HANDLE;
    VkQueryType type = VK_QUERY_TYPE_MAX_ENUM;
    uint32_t query_count = 0;
    VkQueryPipelineStatisticFlags pipeline_statistics = 0;
};

class QueryState {
public:
    void add_query_pool(VkDevice device,
                        VkQueryPool local,
                        VkQueryPool remote,
                        VkQueryType type,
                        uint32_t query_count,
                        VkQueryPipelineStatisticFlags pipeline_statistics);
    void remove_query_pool(VkQueryPool pool);
    VkQueryPool get_remote_query_pool(VkQueryPool pool) const;
    VkDevice get_query_pool_device(VkQueryPool pool) const;
    VkQueryType get_query_pool_type(VkQueryPool pool) const;
    uint32_t get_query_pool_count(VkQueryPool pool) const;
    bool validate_query_range(VkQueryPool pool, uint32_t first_query, uint32_t query_count) const;
    void remove_device(VkDevice device);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, QueryPoolInfo> query_pools_;
};

extern QueryState g_query_state;

} // namespace venus_plus

#endif // VENUS_PLUS_QUERY_STATE_H
