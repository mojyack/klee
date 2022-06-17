#pragma once
#include <array>
#include <optional>

namespace usb {
template <class K, class V, size_t N = 16>
class ArrayMap {
  private:
    std::array<std::optional<std::pair<K, V>>, N> data;

  public:
    auto get(const K& key) const -> std::optional<V> {
        for(const auto& d : data) {
            if(d && d->first == key) {
                return d->second;
            }
        }
        return std::nullopt;
    }

    auto set(const K& key, const V& value) -> bool {
        for(auto& d : data) {
            if(!d) {
                d = std::pair{key, value};
                return true;
            }
        }
        return false;
    }

    auto erase(const K& key) -> bool {
        for(auto& d : data) {
            if(d && d->first == key) {
                d.reset();
                return true;
            }
        }
        return false;
    }
};
} // namespace usb
