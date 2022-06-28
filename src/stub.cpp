#include <stdlib.h>
#include <errno.h>
#include <malloc.h>

extern "C" {
typedef int pid_t;

void read() {}
void lseek() {}
void write() {}
void close() {}
void isatty() {}
void fstat() {}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* ptr = memalign(alignment, size);
    if(ptr == nullptr) {
        return errno;
    }
    *memptr = ptr;
    return 0;
}
}
