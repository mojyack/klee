#pragma once
#include "../debug.hpp"
#include "type.hpp"

namespace interrupt {
template <std::integral T>
auto print_hex(const T value) -> std::array<char, sizeof(T) * 2 + 1> {
    auto r = std::array<char, sizeof(T) * 2 + 1>();

    for(auto i = 0; i < sizeof(T) * 2; i += 1) {
        auto c = (value >> 4 * (sizeof(T) - i - 1)) & 0x0Fu;
        if(c >= 10) {
            c += 'a' - 10;
        } else {
            c += '0';
        }
        r[i] = c;
    }

    r[r.size() - 1] = '\0';

    return r;
}

inline auto print_stackframe(const InterruptFrame& frame) -> void {
    debug::debug_print("CS");
    debug::debug_print(print_hex(frame.cs).data());
    debug::debug_print("RIP");
    debug::debug_print(print_hex(frame.rip).data());
    debug::debug_print("RFLAGS");
    debug::debug_print(print_hex(frame.rflags).data());
    debug::debug_print("SS");
    debug::debug_print(print_hex(frame.ss).data());
    debug::debug_print("RSP");
    debug::debug_print(print_hex(frame.rsp).data());
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winterrupt-service-routine"

#define int_handler_with_error(name)                                                                                          \
    __attribute__((interrupt)) static auto int_handler_##name(InterruptFrame* const frame, const uint64_t error_code)->void { \
        debug::debug_print("interrupt(" #name ")");                                                                           \
        debug::debug_print("code");                                                                                           \
        print_hex(error_code);                                                                                                \
        print_stackframe(*frame);                                                                                             \
        while(true) {                                                                                                         \
            __asm__("hlt");                                                                                                   \
        }                                                                                                                     \
    }

#define int_handler(name)                                                                          \
    __attribute__((interrupt)) static auto int_handler_##name(InterruptFrame* const frame)->void { \
        debug::debug_print("interrupt(" #name ")");                                                \
        print_stackframe(*frame);                                                                  \
        while(true) {                                                                              \
            __asm__("hlt");                                                                        \
        }                                                                                          \
    }

int_handler(divide_error);
int_handler(debug);
int_handler(nmi);
int_handler(breakpoint);
int_handler(overflow);
int_handler(bound_range_exceeded);
int_handler(invalid_opcode);
int_handler(device_not_available);
int_handler_with_error(double_fault);
int_handler(coprocessor_segment_overrun);
int_handler_with_error(invalid_tss);
int_handler_with_error(segment_not_present);
int_handler_with_error(stack_fault);
int_handler_with_error(general_protection);
int_handler_with_error(page_fault);
int_handler(fpu_floating_point);
int_handler_with_error(alignment_check);
int_handler(machine_check);
int_handler(simd_floating_point);
int_handler(virtualization);
int_handler_with_error(control_protection);

#undef int_handler_with_error
#undef int_handler

#pragma clang diagnostic pop
} // namespace interrupt
