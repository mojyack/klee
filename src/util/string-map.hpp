#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef CUTIL_NS
namespace CUTIL_NS {
#endif

namespace internal {
struct StringCompare {
    using is_transparent = void;

    auto operator()(const std::string_view key, const std::string_view str) const -> bool {
        return key == str;
    }
};

struct StringHash {
    using is_transparent        = void;
    using transparent_key_equal = StringCompare;
    using hash_type             = std::hash<std::string_view>;

    auto operator()(const std::string_view str) const -> size_t {
        return hash_type{}(str);
    }

    auto operator()(const std::string& str) const -> size_t {
        return hash_type{}(str);
    }

    auto operator()(const char* const str) const -> size_t {
        return hash_type{}(str);
    }
};
} // namespace internal

template <class T>
using StringMap = std::unordered_map<std::string, T, internal::StringHash, internal::StringCompare>;

#ifdef CUTIL_NS
}
#endif
