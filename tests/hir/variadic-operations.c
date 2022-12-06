#include <stdarg.h>
static int first(int count, ...) {
    va_list list;
    va_list copy;
    va_start(list, count);
    va_copy(copy, list);
    int value = va_arg(copy, int);
    va_end(copy);
    va_end(list);
    return value + count;
}
int main(void) { return first(2, 5); }
