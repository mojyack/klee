#include "libc-support.hpp"
#include "debug.hpp"

extern "C" {
void _exit(void) {
    while(true) {
        __asm__("hlt");
    }
}

caddr_t sbrk(const int incr) {
    // debug stuff
    static auto initial_break = caddr_t(0);
    static auto size_bytes    = size_t(0);
    if(initial_break == 0) {
        initial_break = program_break;
        size_bytes    = program_break_end - program_break;
    }
    {
        using namespace debug;
        const auto remain  = program_break_end - program_break - incr;
        const auto percent = 1. * remain / size_bytes * 100;
        println("sbrk: ", Number(remain, 16, 0), "/", Number(size_bytes, 16, 0), " (", Number(percent, 10, 0), "%)");
    }
    // ~debug stuff

    if(program_break == 0 || program_break + incr >= program_break_end) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    const auto prev_break = program_break;
    program_break += incr;
    return prev_break;
}

int getpid(void) {
    return 1;
}

int kill(const int pid, const int sig) {
    errno = EINVAL;
    return -1;
}
}
