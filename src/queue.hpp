#pragma once
#include <array>

#include "error.hpp"

template <class T>
class ArrayQueue {
  private:
    T*     data;
    size_t capacity;
    size_t read_pos  = 0;
    size_t write_pos = 0;
    size_t size      = 0;

  public:
    auto push(const T& v) -> Error {
        if(size == capacity) {
            return Error::Code::Full;
        }
        data[write_pos] = v;
        size += 1;
        write_pos = (write_pos + 1) % capacity;
        return Error::Code::Success;
    }

    auto pop() -> Error {
        if(size == 0) {
            return Error::Code::Empty;
        }
        size -= 1;
        read_pos = (read_pos + 1) % capacity;
        return Error::Code::Success;
    }

    auto get_size() const -> size_t {
        return size;
    }

    auto is_empty() const -> bool {
        return size == 0;
    }

    auto get_capacity() const -> size_t {
        return capacity;
    }

    auto get_front() const -> const T& {
        return data[read_pos];
    }

    template <size_t cap>
    ArrayQueue(std::array<T, cap>& buf) : data(buf.data()), capacity(cap) {}
    ArrayQueue(T* const buf, const size_t cap) : data(buf), capacity(cap) {}
};
