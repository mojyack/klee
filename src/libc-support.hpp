#pragma once
#include <errno.h>
#include <sys/types.h>

inline auto program_break     = caddr_t();
inline auto program_break_end = caddr_t();

extern "C" {
void    _exit(void);
caddr_t sbrk(const int incr);
int     getpid(void);
int     kill(const int pid, const int sig);
}
