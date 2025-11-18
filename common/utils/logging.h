#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace venus_plus {

// Log levels
enum class LogLevel {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5
};

// Log categories
enum class LogCategory {
    GENERAL,
    NETWORK,
    CLIENT,
    SERVER,
    PROTOCOL,
    VULKAN,
    MEMORY,
    SYNC
};

// Logger singleton
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) { level_ = level; }
    LogLevel get_level() const { return level_; }

    void log(LogLevel level, LogCategory category, const char* file, int line,
             const char* fmt, ...) __attribute__((format(printf, 6, 7))) {
        va_list args;
        va_start(args, fmt);
        logv(level, category, file, line, fmt, args);
        va_end(args);
    }

    void logv(LogLevel level, LogCategory category, const char* file, int line,
              const char* fmt, va_list args) {
        if (level > level_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&time));

        // Extract filename from path
        const char* filename = std::strrchr(file, '/');
        filename = filename ? filename + 1 : file;

        // Print header
        std::fprintf(stderr, "[%s.%03d] [%s] [%s] %s:%d: ",
                     time_buf, static_cast<int>(ms.count()),
                     level_str(level), category_str(category),
                     filename, line);

        // Print message
        std::vfprintf(stderr, fmt, args);

        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }

private:
    Logger() {
        // Initialize from environment
        if (const char* env = std::getenv("VENUS_LOG_LEVEL")) {
            if (std::strcmp(env, "NONE") == 0) level_ = LogLevel::NONE;
            else if (std::strcmp(env, "ERROR") == 0) level_ = LogLevel::ERROR;
            else if (std::strcmp(env, "WARN") == 0) level_ = LogLevel::WARN;
            else if (std::strcmp(env, "INFO") == 0) level_ = LogLevel::INFO;
            else if (std::strcmp(env, "DEBUG") == 0) level_ = LogLevel::DEBUG;
            else if (std::strcmp(env, "TRACE") == 0) level_ = LogLevel::TRACE;
        }
    }

    const char* level_str(LogLevel level) {
        switch (level) {
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::TRACE: return "TRACE";
            default: return "?????";
        }
    }

    const char* category_str(LogCategory cat) {
        switch (cat) {
            case LogCategory::GENERAL:  return "GENERAL ";
            case LogCategory::NETWORK:  return "NETWORK ";
            case LogCategory::CLIENT:   return "CLIENT  ";
            case LogCategory::SERVER:   return "SERVER  ";
            case LogCategory::PROTOCOL: return "PROTOCOL";
            case LogCategory::VULKAN:   return "VULKAN  ";
            case LogCategory::MEMORY:   return "MEMORY  ";
            case LogCategory::SYNC:     return "SYNC    ";
            default: return "????????";
        }
    }

    LogLevel level_ = LogLevel::INFO;
    std::mutex mutex_;
};

} // namespace venus_plus

namespace venus_plus {

// Stream-based helper that buffers message text until destruction.
class LogStream {
public:
    using OstreamManipulator = std::ostream& (*)(std::ostream&);
    using IosManipulator = std::ios_base& (*)(std::ios_base&);

    LogStream(LogLevel level, LogCategory category, const char* file, int line)
        : level_(level), category_(category), file_(file), line_(line) {}

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    LogStream(LogStream&& other) noexcept
        : stream_(std::move(other.stream_)),
          level_(other.level_),
          category_(other.category_),
          file_(other.file_),
          line_(other.line_),
          flushed_(other.flushed_) {
        other.flushed_ = true;
    }

    LogStream& operator=(LogStream&& other) noexcept {
        if (this != &other) {
            flush_if_needed();
            stream_ = std::move(other.stream_);
            level_ = other.level_;
            category_ = other.category_;
            file_ = other.file_;
            line_ = other.line_;
            flushed_ = other.flushed_;
            other.flushed_ = true;
        }
        return *this;
    }

    ~LogStream() { flush_if_needed(); }

    template <typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    LogStream& operator<<(OstreamManipulator manip) {
        manip(stream_);
        return *this;
    }

    LogStream& operator<<(IosManipulator manip) {
        manip(stream_);
        return *this;
    }

private:
    void flush_if_needed() {
        if (flushed_) return;
        auto message = stream_.str();
        const bool had_raw_output = !message.empty();
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        if (!message.empty()) {
            Logger::instance().log(level_, category_, file_, line_, "%s", message.c_str());
        } else if (had_raw_output) {
            Logger::instance().log(level_, category_, file_, line_, "%s", "");
        }
        flushed_ = true;
    }

    std::ostringstream stream_;
    LogLevel level_;
    LogCategory category_;
    const char* file_;
    int line_;
    bool flushed_ = false;
};

} // namespace venus_plus

// Convenience macros
#define VP_LOG(level, category, ...) \
    venus_plus::Logger::instance().log(level, category, __FILE__, __LINE__, __VA_ARGS__)

#define VP_LOG_ERROR(category, ...) \
    VP_LOG(venus_plus::LogLevel::ERROR, venus_plus::LogCategory::category, __VA_ARGS__)

#define VP_LOG_WARN(category, ...) \
    VP_LOG(venus_plus::LogLevel::WARN, venus_plus::LogCategory::category, __VA_ARGS__)

#define VP_LOG_INFO(category, ...) \
    VP_LOG(venus_plus::LogLevel::INFO, venus_plus::LogCategory::category, __VA_ARGS__)

#define VP_LOG_DEBUG(category, ...) \
    VP_LOG(venus_plus::LogLevel::DEBUG, venus_plus::LogCategory::category, __VA_ARGS__)

#define VP_LOG_TRACE(category, ...) \
    VP_LOG(venus_plus::LogLevel::TRACE, venus_plus::LogCategory::category, __VA_ARGS__)

// Set log level programmatically
#define VP_SET_LOG_LEVEL(level) \
    venus_plus::Logger::instance().set_level(venus_plus::LogLevel::level)

#define VP_LOG_STREAM(level, category) \
    venus_plus::LogStream(venus_plus::LogLevel::level, venus_plus::LogCategory::category, __FILE__, __LINE__)

#define VP_LOG_STREAM_ERROR(category) \
    VP_LOG_STREAM(ERROR, category)

#define VP_LOG_STREAM_WARN(category) \
    VP_LOG_STREAM(WARN, category)

#define VP_LOG_STREAM_INFO(category) \
    VP_LOG_STREAM(INFO, category)

#define VP_LOG_STREAM_DEBUG(category) \
    VP_LOG_STREAM(DEBUG, category)

#define VP_LOG_STREAM_TRACE(category) \
    VP_LOG_STREAM(TRACE, category)
