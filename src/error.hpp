#pragma once
#include <array>

class Error {
  public:
    enum class Code {
        Success = 0,
        Full,
        Empty,
        LastOfCode,
    };

  private:
    static constexpr std::array<const char*, 4> codestr = {
        "Success",
        "Full",
        "Empty",
        "LastOfCode",
    };

    Code code;

  public:
    auto to_str() const -> const char* {
        return codestr[static_cast<size_t>(code)];
    }

    operator bool() const {
        return code != Code::Success;
    }

    Error(const Code code) : code(code) {}
};
