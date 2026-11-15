#include <ezmu/foundation/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using ezmu::foundation::ILogSink;
using ezmu::foundation::Logger;
using ezmu::foundation::LogLevel;
using ezmu::foundation::LogRecord;

struct Entry {
    LogLevel level;
    std::string category;
    std::string message;
    std::string file;
};

class RecordingSink final : public ILogSink {
public:
    void write(const LogRecord& record) override {
        entries.push_back(Entry{.level = record.level,
                                .category = std::string(record.category),
                                .message = std::string(record.message),
                                .file = record.location.file_name()});
    }
    void flush() override { ++flushes; }

    std::vector<Entry> entries;
    int flushes = 0;
};

RecordingSink& addRecorder(Logger& logger) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): we just created it.
    return static_cast<RecordingSink&>(logger.addSink(std::make_unique<RecordingSink>()));
}

TEST_CASE("Log levels have stable names", "[log]") {
    CHECK(toString(LogLevel::kTrace) == "trace");
    CHECK(toString(LogLevel::kDebug) == "debug");
    CHECK(toString(LogLevel::kInfo) == "info");
    CHECK(toString(LogLevel::kWarning) == "warning");
    CHECK(toString(LogLevel::kError) == "error");
    CHECK(toString(LogLevel::kOff) == "off");
}

TEST_CASE("Logger formats messages and delivers them to every sink", "[log]") {
    Logger logger;
    RecordingSink& first = addRecorder(logger);
    RecordingSink& second = addRecorder(logger);

    logger.warning("rhi", "adapter {} has {} queues", "lavapipe", 1);

    for (const RecordingSink* sink : {&first, &second}) {
        REQUIRE(sink->entries.size() == 1);
        CHECK(sink->entries[0].level == LogLevel::kWarning);
        CHECK(sink->entries[0].category == "rhi");
        CHECK(sink->entries[0].message == "adapter lavapipe has 1 queues");
    }
}

TEST_CASE("Records below the minimum level are dropped", "[log]") {
    Logger logger(LogLevel::kWarning);
    RecordingSink& sink = addRecorder(logger);

    logger.info("test", "dropped");
    logger.log(LogLevel::kDebug, "test", "dropped");
    logger.error("test", "kept");

    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].message == "kept");
    CHECK_FALSE(logger.isEnabled(LogLevel::kInfo));
    CHECK(logger.isEnabled(LogLevel::kError));
}

TEST_CASE("The off level disables logging and is never written", "[log]") {
    Logger logger(LogLevel::kTrace);
    RecordingSink& sink = addRecorder(logger);
    logger.write(LogLevel::kOff, "test", "never");
    CHECK(sink.entries.empty());

    logger.setMinLevel(LogLevel::kOff);
    CHECK(logger.minLevel() == LogLevel::kOff);
    logger.error("test", "disabled");
    CHECK(sink.entries.empty());
}

TEST_CASE("Writing a pre-formatted message records the caller's location", "[log]") {
    Logger logger;
    RecordingSink& sink = addRecorder(logger);
    logger.write(LogLevel::kInfo, "test", "here");
    REQUIRE(sink.entries.size() == 1);
    CHECK(sink.entries[0].file.find("log_tests.cpp") != std::string::npos);
}

TEST_CASE("Flush reaches every sink", "[log]") {
    Logger logger;
    RecordingSink& sink = addRecorder(logger);
    logger.flush();
    CHECK(sink.flushes == 1);
}

TEST_CASE("Logging from several threads delivers every record", "[log]") {
    Logger logger;
    RecordingSink& sink = addRecorder(logger);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 100;
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&logger, t] {
                for (int i = 0; i < kPerThread; ++i) {
                    logger.info("thread", "{} {}", t, i);
                }
            });
        }
    }
    CHECK(sink.entries.size() == static_cast<std::size_t>(kThreads * kPerThread));
}

} // namespace
