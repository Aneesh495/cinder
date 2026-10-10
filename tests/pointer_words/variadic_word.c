#include <stdint.h>
#include <stdarg.h>
static uintptr_t take(int count, ...) {
    va_list list;
    va_start(list, count);
    uintptr_t word = va_arg(list, uintptr_t);
    va_end(list);
    return word ^ (uintptr_t)0x1234;
}
int main(void) { int value = 53; uintptr_t word = (uintptr_t)(void *)&value ^ (uintptr_t)0x1234; return *(int *)(void *)take(1, word); }
