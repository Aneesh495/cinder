#include <stdarg.h>
int sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first = va_arg(ap, int);
    int second = va_arg(ap, int);
    int third = va_arg(ap, int);
    va_end(ap);
    return first + second + third;
}
int main(void) { return sum(3, 10, 20, 12); }
