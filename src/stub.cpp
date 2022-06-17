extern "C" {
typedef int pid_t;

void sbrk() {}
void read() {}
void lseek() {}
void write() {}
void close() {}
void isatty() {}
void fstat() {}
void _exit() {}
int  kill(pid_t, int) { return 0; }
void getpid() {}

void __cxa_pure_virtual() {
    while(1) {
        __asm__("hlt");
    }
}
void __cxa_guard_acquire() {}
void __cxa_guard_release() {}

}

auto operator new(unsigned long) -> void* { return nullptr; }
auto operator delete(void*) -> void {}
