#include <ezmu/foundation/log.hpp>

#include <cstdio>
#include <print>

namespace ezmu::foundation {

std::string_view toString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::kTrace:
        return "trace";
    case LogLevel::kDebug:
        return "debug";
    case LogLevel::kInfo:
        return "info";
    case LogLevel::kWarning:
        return "warning";
    case LogLevel::kError:
        return "error";
    case LogLevel::kOff:
        return "off";
    }
    return "unknown";
}

void StderrLogSink::write(const LogRecord& record) {
    std::println(stderr, "[ezMU] [{}] {}: {}", toString(record.level), record.category,
                 record.message);
    if (record.level >= LogLevel::kError) {
        std::fflush(stderr);
    }
}

void StderrLogSink::flush() {
    std::fflush(stderr);
}

Logger::Logger(LogLevel minLevel) noexcept : m_minLevel(minLevel) {}

ILogSink& Logger::addSink(std::unique_ptr<ILogSink> sink) {
    const std::scoped_lock lock(m_mutex);
    return *m_sinks.emplace_back(std::move(sink));
}

void Logger::setMinLevel(LogLevel level) noexcept {
    m_minLevel.store(level, std::memory_order_relaxed);
}

LogLevel Logger::minLevel() const noexcept {
    return m_minLevel.load(std::memory_order_relaxed);
}

bool Logger::isEnabled(LogLevel level) const noexcept {
    return level != LogLevel::kOff && level >= minLevel();
}

void Logger::write(LogLevel level, std::string_view category, std::string_view message,
                   std::source_location location) {
    if (isEnabled(level)) {
        writeRecord(LogRecord{
            .level = level, .category = category, .message = message, .location = location});
    }
}

void Logger::flush() {
    const std::scoped_lock lock(m_mutex);
    for (const auto& sink : m_sinks) {
        sink->flush();
    }
}

void Logger::writeRecord(const LogRecord& record) {
    const std::scoped_lock lock(m_mutex);
    for (const auto& sink : m_sinks) {
        sink->write(record);
    }
}

} // namespace ezmu::foundation
