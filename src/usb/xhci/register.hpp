#pragma once
#include <cstddef>
#include <cstdint>

template <class>
struct ArrayLength {};

template <class T, size_t size>
struct ArrayLength<T[size]> {
    static constexpr auto value = size;
};

template <class T>
class MemoryMappedRegister {
  private:
    volatile T            value;
    static constexpr auto len = ArrayLength<decltype(T::data)>::value;

  public:
    auto read() const -> T {
        auto r = T();
        for(auto i = size_t(0); i < len; i += 1) {
            r.data[i] = value.data[i];
        }
        return r;
    }

    auto write(const T& data) -> void {
        for(auto i = size_t(0); i < len; i += 1) {
            value.data[i] = data.data[i];
        }
    }
};

template <class T>
struct DefaultBitmap {
    T data[1];

    auto operator=(const T& value) -> DefaultBitmap& {
        data[0] = value;
        return *this;
    }

    operator T() const {
        return data[0];
    }
};

template <class T>
class ArrayWrapper {
  private:
    T*     array;
    size_t size;

  public:
    auto get_size() const -> size_t {
        return size;
    }

    auto begin() -> T* {
        return array;
    }

    auto end() -> T* {
        return array + size;
    }

    auto cbegin() const -> const T* {
        return array;
    }

    auto cend() const -> const T* {
        return array + size;
    }

    auto operator[](const size_t i) -> T& {
        return array[i];
    }

    ArrayWrapper(const uintptr_t array, const size_t size) : array(reinterpret_cast<T*>(array)), size(size) {}
};
