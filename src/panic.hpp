#pragma once
#include "debug.hpp"
#include "error.hpp"

template <debug::Printable... Args>
auto fatal_error(const Args... args) -> void {
    debug::println(args...);
    while(true) {
        __asm__("hlt");
    }
}

template <debug::Printable... Args>
auto fatal_assert(const bool cond, const Args... args) -> void {
    if(!cond) {
        fatal_error(args...);
    }
}
