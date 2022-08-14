#pragma once
#include <array>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../memory-manager.hpp"
#include "../message.hpp"
#include "../paging.hpp"
#include "../segment.hpp"

namespace task {
struct TaskContext {
    uint64_t                 cr3, rip, rflags, reserved1;            // offset 0x00
    uint64_t                 cs, ss, fs, gs;                         // offset 0x20
    uint64_t                 rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp; // offset 0x40
    uint64_t                 r8, r9, r10, r11, r12, r13, r14, r15;   // offset 0x80
    std::array<uint8_t, 512> fxsave_area;                            // offset 0xc0
} __attribute__((packed));

struct PageMap {
    paging::PageDirectoryPointerTable upper_page_map; // 0xFFFF800000000000 ~ 0xFFFF807FFFFFFFFF
    std::vector<SmartFrameID>         allocated_frames;
};

using TaskEntry = void(uint64_t data, int64_t id);

class Task {
  private:
    uint64_t              id;
    std::vector<uint64_t> stack;
    std::deque<Message>   messages;
    TaskEntry*            entry = nullptr;
    alignas(16) TaskContext context;

    // 0000RESERVED0000 00PML4000 00PDPT000 000PD0000 000PT0000 000OFFSET000
    // 0000000000000000 000000000 000000000 000000000 000000000 000000000000   // implementation limit of klee
    // or               or        ~         ~         ~         ~
    // 1111111111111111 100000000 111111111 111111111 111111111 111111111111

    std::unique_ptr<PageMap> page_map;

  public:
    static constexpr auto default_stack_bytes = size_t(4096);

    auto get_id() const -> uint64_t {
        return id;
    }

    auto init_context(TaskEntry* const func, const int64_t data) -> Error {
        entry = func;

        const auto stack_size = default_stack_bytes / sizeof(stack[0]);
        stack.resize(stack_size);
        const auto stack_end = reinterpret_cast<uint64_t>(&stack[stack_size]);

        memset(&context, 0, sizeof(context));
        context.rip = reinterpret_cast<uint64_t>(func);
        context.rdi = id;
        context.rsi = data;

        context.cr3    = get_cr3();
        context.rflags = 0x202;
        context.cs     = kernel_cs;
        context.ss     = kernel_ss;
        context.rsp    = (stack_end & ~0x0Flu) - 8;

        // mask all exceptions of MXCSR
        *reinterpret_cast<uint32_t*>(&context.fxsave_area[24]) = 0x1f80;

        return Error();
    }

    auto send_message(Message message) -> void {
        messages.push_back(std::move(message));
        wakeup();
    }

    auto send_message_may_fail(Message message) -> void {
        messages.push_back(std::move(message));
        wakeup_may_fail();
    }

    auto receive_message() -> std::optional<Message> {
        if(messages.empty()) {
            return std::nullopt;
        }

        auto m = messages.front();
        messages.pop_front();
        return m;
    }

    auto exit() -> void;
    auto sleep() -> Task&;
    auto wakeup(int nice = -1) -> Task&;
    auto wakeup_may_fail() -> void; // used by interrupt
    auto wait_address(const void* const address) -> void;

    // for task_manager

    auto get_context() -> TaskContext& {
        return context;
    }

    auto get_page_map() -> std::unique_ptr<PageMap>& {
        return page_map;
    }

    auto apply_page_map() -> void {
        auto& pml4e             = paging::pml4_table[0b100000000];
        if(page_map) {
            pml4e.data              = reinterpret_cast<uint64_t>(page_map->upper_page_map.data.data());
            pml4e.directory.present = 1;
            pml4e.directory.write   = 1;
            pml4e.directory.user    = 1;
        } else {
            pml4e.data  = 0;
        }
    }

    Task(const uint64_t id) : id(id) {}
};
} // namespace task
