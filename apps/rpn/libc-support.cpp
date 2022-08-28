#include <errno.h>
#include <sys/types.h>

extern "C" {
auto _exit(void) -> void {
    while(true) {
        __asm__("hlt");
    }
}

auto sbrk(const int incr) -> caddr_t {
    errno = ENOMEM;
    return (caddr_t)-1;
}

auto getpid(void) -> int {
    return 1;
}

auto kill(const int pid, const int sig) -> int {
    errno = EINVAL;
    return -1;
}

auto close(const int fd) -> int {
    errno = EBADF;
    return -1;
}

auto lseek(const int fd, const off_t offset, const int whence) -> off_t {
    errno = EBADF;
    return -1;
}

auto read(const int fd, void* const buf, const size_t count) -> ssize_t {
    errno = EBADF;
    return -1;
}

auto write(const int fd, const void* const buf, const size_t count) -> ssize_t {
    errno = EBADF;
    return -1;
}

auto fstat(const int fd, struct stat* const buf) -> int {
    errno = EBADF;
    return -1;
}

auto isatty(const int fd) -> int {
    errno = EBADF;
    return -1;
}
}
