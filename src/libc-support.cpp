#include "libc-support.hpp"
//#include "print.hpp"

extern "C" {
void _exit(void) {
    while(true) {
        __asm__("hlt");
    }
}

caddr_t sbrk(const int incr) {
   // constexpr auto heap_bytes = 64 * 512 * 4096;
   // 
   // const auto usage = 100.0 * (program_break_end - program_break - incr) / heap_bytes;
   // printk("heap %d%%\n", int(usage));
    if(program_break == 0 || program_break + incr >= program_break_end) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    caddr_t prev_break = program_break;
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
