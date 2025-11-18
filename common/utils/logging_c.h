#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VP_LOG_LEVEL_NONE = 0,
    VP_LOG_LEVEL_ERROR = 1,
    VP_LOG_LEVEL_WARN = 2,
    VP_LOG_LEVEL_INFO = 3,
    VP_LOG_LEVEL_DEBUG = 4,
    VP_LOG_LEVEL_TRACE = 5,
} vp_log_level;

typedef enum {
    VP_LOG_CATEGORY_GENERAL,
    VP_LOG_CATEGORY_NETWORK,
    VP_LOG_CATEGORY_CLIENT,
    VP_LOG_CATEGORY_SERVER,
    VP_LOG_CATEGORY_PROTOCOL,
    VP_LOG_CATEGORY_VULKAN,
    VP_LOG_CATEGORY_MEMORY,
    VP_LOG_CATEGORY_SYNC,
} vp_log_category;

void vp_log_printf(vp_log_level level,
                   vp_log_category category,
                   const char* file,
                   int line,
                   const char* fmt,
                   ...) __attribute__((format(printf, 5, 6)));

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus

#define VP_LOG(level, category, ...) \
    vp_log_printf(VP_LOG_LEVEL_##level, VP_LOG_CATEGORY_##category, __FILE__, __LINE__, __VA_ARGS__)

#define VP_LOG_ERROR(category, ...) VP_LOG(ERROR, category, __VA_ARGS__)
#define VP_LOG_WARN(category, ...)  VP_LOG(WARN, category, __VA_ARGS__)
#define VP_LOG_INFO(category, ...)  VP_LOG(INFO, category, __VA_ARGS__)
#define VP_LOG_DEBUG(category, ...) VP_LOG(DEBUG, category, __VA_ARGS__)
#define VP_LOG_TRACE(category, ...) VP_LOG(TRACE, category, __VA_ARGS__)

#endif
