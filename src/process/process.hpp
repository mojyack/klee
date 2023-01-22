#pragma once
#include <deque>
#include <vector>

#include "../memory-manager.hpp"
#include "../message.hpp"
#include "../paging.hpp"
#include "../segment.hpp"
#include "../smp/id.hpp"
#include "../util/container-of.hpp"
#include "../util/dense-map.hpp"

namespace process {
struct alignas(16) ThreadContext {
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

using ThreadEntry = void(uint64_t data, int64_t id);

using Nice      = int32_t;
using EventID   = uint32_t;
using ProcessID = uint32_t;
using ThreadID  = uint32_t;

constexpr auto invalid_event = EventID(-1);

template <class K, class T>
using IDMap = dense_map::DenseMap<K, std::unique_ptr<T>>;

using AutoLock = mutex_like::AutoMutex<spinlock::SpinLock>;

struct Process;

struct Thread {
    using StackUnitType = uint64_t;

    const uint64_t id;
    Process* const process;
    uintptr_t      system_stack_address;

    ThreadEntry*               entry = nullptr;
    std::vector<StackUnitType> stack;
    ThreadContext              context;

    smp::ProcessorNumber running_on = smp::invalid_processor_number;
    std::vector<EventID> events;
    Nice                 nice         = 0;
    size_t               suspend_from = 0;
    size_t               suspend_for  = 0;
    bool                 zombie       = false;
    bool                 movable      = true;

    // 0000RESERVED0000 00PML4000 00PDPT000 000PD0000 000PT0000 000OFFSET000
    // 0000000000000000 000000000 000000000 000000000 000000000 000000000000   // implementation limit of klee
    // or               or        ~         ~         ~         ~
    // 1111111111111111 100000000 111111111 111111111 111111111 111111111111

    auto init_context(ThreadEntry* const func, const int64_t data) -> void {
        constexpr auto default_stack_bytes = size_t(4096);
        constexpr auto default_stack_count = default_stack_bytes / sizeof(StackUnitType);

        entry = func;
        stack.resize(default_stack_count);
        const auto stack_end = reinterpret_cast<uint64_t>(&stack[default_stack_count]);

        memset(&context, 0, sizeof(context));
        context.rip = reinterpret_cast<uint64_t>(func);
        context.rdi = id;
        context.rsi = data;

        context.cr3    = get_cr3();
        context.rflags = 0x202;
        context.cs     = segment::kernel_cs.data;
        context.ss     = segment::kernel_ss.data;
        context.rsp    = (stack_end & ~0x0Flu) - 8;

        // mask all exceptions of MXCSR
        *reinterpret_cast<uint32_t*>(&context.fxsave_area[24]) = 0x1f80;
    }

    Thread(const uint64_t id, Process* const process) : id(id), process(process) {}
};

struct Process {
    const uint64_t id;

    std::unique_ptr<PageMap> page_map;
    spinlock::SpinLock       page_map_mutex;

    IDMap<ThreadID, Thread> threads;

    auto apply_page_map(const AutoLock& /*lock*/, paging::PML4Table& pml4_table) -> void {
        auto& pml4e = pml4_table.data[0b100000000];
        if(page_map) {
            pml4e.data              = reinterpret_cast<uint64_t>(page_map->upper_page_map.data.data());
            pml4e.directory.present = 1;
            pml4e.directory.write   = 1;
            pml4e.directory.user    = 1;
        } else {
            pml4e.data = 0;
        }
    }

    Process(const uint64_t id) : id(id) {}
};
} // namespace process
