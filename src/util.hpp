#pragma once
#include <cstdint>
#include <cstdlib>

template <class T, class M>
inline constexpr auto offset_of(const M T::*member) -> ptrdiff_t {
    return reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<T*>(0)->*member));
}

template <class T, class M>
inline constexpr auto container_of(M* ptr, const M T::*member) -> T* {
    return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(ptr) - offset_of(member));
}
