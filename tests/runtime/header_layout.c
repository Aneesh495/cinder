#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
_Static_assert(sizeof(size_t) == 8 && sizeof(ptrdiff_t) == 8, "LP64");
_Static_assert(sizeof(time_t) == 8 && sizeof(pid_t) == 4, "POSIX scalars");
#if defined(__CINDER__) || defined(__linux__)
_Static_assert(sizeof(mode_t) == 4, "Linux mode_t");
#endif
_Static_assert(sizeof(struct tm) == 56 && _Alignof(struct tm) == 8, "Linux tm extent");
_Static_assert(offsetof(struct tm, tm_year) == 20 && offsetof(struct tm, tm_isdst) == 32, "tm fields");
#if defined(__CINDER__) || defined(__x86_64__)
_Static_assert(sizeof(va_list) == 24 && _Alignof(va_list) == 8, "SysV variadic state");
#endif
int main(void) { int exited = 23 << 8; int signaled = 15; return !WIFEXITED(exited) || WEXITSTATUS(exited) != 23 || WIFEXITED(signaled); }
