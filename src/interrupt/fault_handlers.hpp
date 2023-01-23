#pragma once
#include "../debug.hpp"
#include "../log.hpp"
#include "../process/manager.hpp"
#include "../segment.hpp"
#include "type.hpp"

namespace interrupt {
inline auto print_stackframe(const InterruptFrame& frame) -> void {
    debug::println("CS: ", frame.cs);
    debug::println("RIP: ", frame.rip);
    debug::println("RFLAGS: ", frame.rflags);
    debug::println("SS: ", frame.ss);
    debug::println("RSP: ", frame.rsp);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winterrupt-service-routine"

#define int_handler_with_error(name)                                                                                          \
    __attribute__((interrupt)) static auto int_handler_##name(InterruptFrame* const frame, const uint64_t error_code)->void { \
        try_kill_app(*frame, #name);                                                                                          \
        debug::println("interrupt(" #name ")");                                                                               \
        debug::println("code: ", error_code);                                                                                 \
        print_stackframe(*frame);                                                                                             \
        while(true) {                                                                                                         \
            __asm__("hlt");                                                                                                   \
        }                                                                                                                     \
    }

#define int_handler(name)                                                                          \
    __attribute__((interrupt)) static auto int_handler_##name(InterruptFrame* const frame)->void { \
        try_kill_app(*frame, #name);                                                               \
        debug::println("interrupt(" #name ")");                                                    \
        print_stackframe(*frame);                                                                  \
        while(true) {                                                                              \
            __asm__("hlt");                                                                        \
        }                                                                                          \
    }

__attribute__((no_caller_saved_registers)) inline auto try_kill_app(const InterruptFrame& frame, const char* const name) -> void {
    if(segment::SegmentSelector{.data = static_cast<uint16_t>(frame.cs)}.bits.rpl != 3) {
        return;
    }

    process::manager->exit_this_thread();
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
