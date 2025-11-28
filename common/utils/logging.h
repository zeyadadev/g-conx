#pragma once

#include <array>
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
    SYNC,
    COUNT
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
    LogLevel get_category_level(LogCategory category) const {
        const size_t idx = static_cast<size_t>(category);
        if (idx >= category_levels_.size()) {
            return level_;
        }
        return category_levels_[idx];
    }
    void set_category_level(LogCategory category, LogLevel level) {
        const size_t idx = static_cast<size_t>(category);
        if (idx < category_levels_.size()) {
            category_levels_[idx] = level;
        }
    }

    void log(LogLevel level, LogCategory category, const char* file, int line,
             const char* fmt, ...) __attribute__((format(printf, 6, 7))) {
        va_list args;
        va_start(args, fmt);
        logv(level, category, file, line, fmt, args);
        va_end(args);
    }

    void logv(LogLevel level, LogCategory category, const char* file, int line,
              const char* fmt, va_list args) {
        const LogLevel threshold = get_category_level(category);
        if (level > threshold) return;

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
    Logger() { initialize_from_env(); }

    static LogLevel parse_level(const char* str, LogLevel fallback) {
        if (!str) {
            return fallback;
        }
        if (std::strcmp(str, "NONE") == 0) return LogLevel::NONE;
        if (std::strcmp(str, "ERROR") == 0) return LogLevel::ERROR;
        if (std::strcmp(str, "WARN") == 0) return LogLevel::WARN;
        if (std::strcmp(str, "INFO") == 0) return LogLevel::INFO;
        if (std::strcmp(str, "DEBUG") == 0) return LogLevel::DEBUG;
        if (std::strcmp(str, "TRACE") == 0) return LogLevel::TRACE;
        return fallback;
    }

    static bool parse_category(const std::string& str, LogCategory* out) {
        if (!out) return false;
        if (str == "GENERAL") { *out = LogCategory::GENERAL; return true; }
        if (str == "NETWORK") { *out = LogCategory::NETWORK; return true; }
        if (str == "CLIENT")  { *out = LogCategory::CLIENT;  return true; }
        if (str == "SERVER")  { *out = LogCategory::SERVER;  return true; }
        if (str == "PROTOCOL"){ *out = LogCategory::PROTOCOL;return true; }
        if (str == "VULKAN")  { *out = LogCategory::VULKAN;  return true; }
        if (str == "MEMORY")  { *out = LogCategory::MEMORY;  return true; }
        if (str == "SYNC")    { *out = LogCategory::SYNC;    return true; }
        return false;
    }

    static std::string trim(const std::string& input) {
        const auto first = input.find_first_not_of(" \t");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = input.find_last_not_of(" \t");
        return input.substr(first, last - first + 1);
    }

    void initialize_from_env() {
        // Default to WARN to avoid noisy INFO-level logs unless explicitly requested.
        level_ = LogLevel::WARN;
        category_levels_.fill(level_);

        if (const char* env = std::getenv("VENUS_LOG_LEVEL")) {
            level_ = parse_level(env, level_);
            category_levels_.fill(level_);
        }

        // Allow per-category overrides via VENUS_LOG_CATEGORIES=MEMORY=INFO,SERVER=WARN
        if (const char* cat_env = std::getenv("VENUS_LOG_CATEGORIES")) {
            std::stringstream ss(cat_env);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token = trim(token);
                if (token.empty()) {
                    continue;
                }
                std::string name = token;
                std::string level_str;
                const auto eq = token.find('=');
                if (eq != std::string::npos) {
                    name = token.substr(0, eq);
                    level_str = token.substr(eq + 1);
                }
                name = trim(name);
                level_str = trim(level_str);
                LogCategory category;
                if (!parse_category(name, &category)) {
                    continue;
                }
                LogLevel lvl = level_;
                if (!level_str.empty()) {
                    lvl = parse_level(level_str.c_str(), lvl);
                }
                set_category_level(category, lvl);
            }
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

    LogLevel level_ = LogLevel::WARN;
    std::array<LogLevel, static_cast<size_t>(LogCategory::COUNT)> category_levels_ = {};
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
