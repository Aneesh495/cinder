#include <stdint.h>
#include <stdarg.h>
static uintptr_t take(int count, ...) {
    va_list list;
    va_start(list, count);
    for (int i = 0; i < count; ++i) (void)va_arg(list, int);
    uintptr_t word = va_arg(list, uintptr_t);
    va_end(list);
    return word;
}
int main(void) { int value = 59; uintptr_t word = (uintptr_t)(void *)&value; return *(int *)(void *)take(8, 1, 2, 3, 4, 5, 6, 7, 8, word); }
