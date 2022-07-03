#pragma once
#include <variant>

#ifdef CUTIL_NS
namespace CUTIL_NS {
#endif

template <class... Ts>
class Variant {
  private:
    template <class>
    struct Tag {};

    using Finder = std::variant<Tag<Ts>...>;

    std::variant<Ts...> data;

  public:
    auto as_variant() -> std::variant<Ts...>& {
        return data;
    }
    auto index() const -> size_t {
        return data.index();
    }

    template <class T>
    constexpr static auto index_of() -> size_t {
        return Finder(Tag<T>{}).index();
    }

    template <class T>
    auto get() -> T& {
        return std::get<T>(data);
    }

    template <class T>
    auto get() const -> const T& {
        return std::get<T>(data);
    }

    template <size_t T>
    auto get() -> auto& {
        return std::get<T>(data);
    }

    template <size_t T>
    auto get() const -> const auto& {
        return std::get<T>(data);
    }

    template <size_t index = 0>
    auto visit(auto visitor) -> auto {
        if constexpr(index < sizeof...(Ts)) {
            if(index == data.index()) {
                return visitor(get<index>());
            }
            return visit<index + 1>(visitor);
        }

        // unreachable
        return decltype(visitor(get<0>()))();
    }

    template <size_t index = 0>
    auto visit(auto visitor) const -> auto {
        if constexpr(index < sizeof...(Ts)) {
            if(index == data.index()) {
                return visitor(get<index>());
            }
            return visit<index + 1>(visitor);
        }

        // unreachable
        return visitor(get<0>());
    }

    template <class T, class... Args>
    auto emplace(Args&&... args) -> Variant<Ts...>& {
        data.template emplace<T>(std::forward<Args>(args)...);
        return *this;
    }

    auto emplace(auto&& o) -> Variant<Ts...>& {
        data = std::move(o);
        return *this;
    }

    Variant() {}
    Variant(auto&& o) : data(std::move(o)) {}

    template <class T, class... Args>
    Variant(std::in_place_type_t<T>, Args&&... args) : data(std::in_place_type<T>, std::forward<Args>(args)...) {}

    template <class... Args>
    Variant(Args&&... args) : data(args...) {}
};

#ifdef CUTIL_NS
}
#endif
