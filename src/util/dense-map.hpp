#pragma once
#include <concepts>
#include <vector>

namespace dense_map {
template <class T, class V>
concept Validator = requires(const T& value) {
                        static_cast<bool>(V::is_valid(value));
                    };

template <class T>
struct DefaultValidator {
    static auto is_valid(const T& value) -> bool {
        return static_cast<bool>(value);
    }
};

static_assert(Validator<int, DefaultValidator<int>>);

template <std::integral K, class T, class V = DefaultValidator<T>>
    requires(Validator<T, V>)
class DenseMap {
  private:
    std::vector<T> data;

  public:
    auto find_empty_slot() -> K {
        const auto limit = K(data.size());
        for(auto i = K(0); i < limit; i += 1) {
            if(!V::is_valid(data[i])) {
                return i;
            }
        }
        data.resize(limit + 1);
        return limit;
    }

    auto contains(const K key) const -> bool {
        return key < data.size() && static_cast<bool>(data[key]);
    }

    auto empty() const -> bool {
        for(const auto& d : data) {
            if(V::is_valid(d)) {
                return false;
            }
        }
        return true;
    }

    auto operator[](const K& key) -> T& {
        return data[key];
    }

    auto operator[](const K& key) const -> const T& {
        return data[key];
    }
};
} // namespace dense_map
