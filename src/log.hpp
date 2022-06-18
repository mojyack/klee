#pragma once
#include "console.hpp"

enum class LogLevel {
    Error = 3,
    Warn  = 4,
    Info  = 6,
    Debug = 7,
};

inline auto operator<=>(const LogLevel lhs, const LogLevel rhs) {
    return static_cast<int>(lhs) <=> static_cast<int>(rhs);
}

class Logger {
  private:
    LogLevel log_level = LogLevel::Info;

  public:
    auto set_log_level(const LogLevel level) -> void {
        log_level = level;
    }

    auto operator()(const LogLevel level, const char* const format, ...) -> int {
        if(level > log_level) {
            return 0;
        }

        static auto buffer = std::array<char, 1024>();

        va_list ap;
        va_start(ap, format);
        const auto result = vsnprintf(buffer.data(), buffer.size(), format, ap);
        va_end(ap);
        console->puts(buffer.data());
        return result;
    }
};

inline auto logger = Logger();
