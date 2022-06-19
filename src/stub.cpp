extern "C" {
typedef int pid_t;

void read() {}
void lseek() {}
void write() {}
void close() {}
void isatty() {}
void fstat() {}

void __cxa_pure_virtual() {
    while(1) {
        __asm__("hlt");
    }
}
}
