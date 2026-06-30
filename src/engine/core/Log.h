#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace aine {

enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogEntry {
    std::uint64_t sequence = 0;
    std::chrono::system_clock::time_point timestamp{};
    LogLevel level = LogLevel::Info;
    std::string message;
};

} // namespace aine
