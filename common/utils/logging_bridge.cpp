#include "utils/logging_c.h"
#include "utils/logging.h"

#include <cstdarg>

extern "C" void vp_log_printf(vp_log_level level,
                               vp_log_category category,
                               const char* file,
                               int line,
                               const char* fmt,
                               ...) {
    va_list args;
    va_start(args, fmt);
    venus_plus::Logger::instance().logv(static_cast<venus_plus::LogLevel>(level),
                                        static_cast<venus_plus::LogCategory>(category),
                                        file,
                                        line,
                                        fmt,
                                        args);
    va_end(args);
}
