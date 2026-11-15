#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>

// Minimal logging: a Logger service fans records out to pluggable sinks. There is no global
// logger (ADR 0018): applications create one and pass it to the services that need it.

namespace ezmu::foundation {

/// Severity of a log record, in increasing order. `kOff` disables logging when used as the
/// minimum level; records are never written at `kOff`.
enum class LogLevel : std::uint8_t {
    kTrace,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kOff,
};

/// Stable lowercase name of a level (e.g. "warning").
[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

/// One log record. The views are valid only for the duration of ILogSink::write().
struct LogRecord {
    LogLevel level = LogLevel::kInfo;
    std::string_view category; ///< Usually the module name, e.g. "rhi".
    std::string_view message;
    std::source_location location; ///< May be empty (no file name) for formatted records.
};

/// Destination of log records (console, file, test recorder, in-game console...).
/// Sinks are called with the Logger's lock held, so calls to one sink are never concurrent;
/// a sink must not call back into the Logger.
class ILogSink {
public:
    ILogSink() = default;
    ILogSink(const ILogSink&) = delete;
    ILogSink& operator=(const ILogSink&) = delete;
    ILogSink(ILogSink&&) = delete;
    ILogSink& operator=(ILogSink&&) = delete;
    virtual ~ILogSink() = default;

    virtual void write(const LogRecord& record) = 0;
    virtual void flush() {}
};

/// Writes "[ezMU] [level] category: message" lines to stderr. Flushes after errors.
class StderrLogSink final : public ILogSink {
public:
    void write(const LogRecord& record) override;
    void flush() override;
};

/// Routes records at or above a minimum level to its sinks.
///
/// Thread-safety: every member function may be called from any thread. Records are delivered
/// to sinks in the order the Logger lock is acquired. Allocation: formatting allocates only
/// when the level is enabled; disabled levels cost one atomic load.
class Logger {
public:
    explicit Logger(LogLevel minLevel = LogLevel::kInfo) noexcept;

    /// Adds a sink (owned by the Logger) and returns a reference to it, valid for the Logger's
    /// lifetime.
    ILogSink& addSink(std::unique_ptr<ILogSink> sink);

    void setMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept;
    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    /// Writes a pre-formatted message, recording the caller's source location.
    void write(LogLevel level, std::string_view category, std::string_view message,
               std::source_location location = std::source_location::current());

    /// Formats with std::format and writes, if `level` is enabled.
    template <typename... Args>
    void log(LogLevel level, std::string_view category, std::format_string<Args...> format,
             Args&&... args) {
        if (isEnabled(level)) {
            writeRecord(LogRecord{.level = level,
                                  .category = category,
                                  .message = std::format(format, std::forward<Args>(args)...),
                                  .location = {}});
        }
    }

    template <typename... Args>
    void info(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::kInfo, category, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warning(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::kWarning, category, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::string_view category, std::format_string<Args...> format, Args&&... args) {
        log(LogLevel::kError, category, format, std::forward<Args>(args)...);
    }

    void flush();

private:
    void writeRecord(const LogRecord& record);

    std::atomic<LogLevel> m_minLevel;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<ILogSink>> m_sinks;
};

} // namespace ezmu::foundation
