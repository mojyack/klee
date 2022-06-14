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
}
