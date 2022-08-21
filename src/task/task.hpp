#pragma once
#include <array>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

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
    uint64_t              system_stack_pointer = 0;
    alignas(16) TaskContext context;

    // 0000RESERVED0000 00PML4000 00PDPT000 000PD0000 000PT0000 000OFFSET000
    // 0000000000000000 000000000 000000000 000000000 000000000 000000000000   // implementation limit of klee
    // or               or        ~         ~         ~         ~
    // 1111111111111111 100000000 111111111 111111111 111111111 111111111111

    std::unique_ptr<PageMap> page_map;

  public:
    static constexpr auto default_stack_bytes = size_t(4096);

    auto get_id() const -> uint64_t;
    auto init_context(TaskEntry* const func, const int64_t data) -> Error;
    auto send_message(Message message) -> void;
    auto send_message_may_fail(Message message) -> void;
    auto receive_message() -> std::optional<Message>;

    auto exit() -> void;
    auto sleep() -> Task&;
    auto wakeup(int nice = -1) -> Task&;
    auto wakeup_may_fail() -> void; // used by interrupt
    auto wait_event(uint64_t event_id) -> void;
    auto wait_events(std::vector<uint64_t> event_ids) -> void;

    // for manager
    auto get_system_stack_pointer() -> uint64_t&;
    auto get_context() -> TaskContext&;
    auto get_page_map() -> std::unique_ptr<PageMap>&;
    auto apply_page_map() -> void;

    Task(const uint64_t id) : id(id) {}
};
} // namespace task
