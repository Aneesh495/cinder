#include <stdarg.h>
struct Payload { long integer; double floating; unsigned char bytes[9]; };
static int clone_arguments(int n, ...) {
    va_list source, copy; va_start(source, n); va_copy(copy, source);
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += va_arg(source, int) + va_arg(copy, int);
    sum += (int)va_arg(source, double) + (int)va_arg(copy, double);
    va_end(copy); va_end(source); return sum;
}
int main(void) {
    struct Payload source = {0}; source.integer = 17; source.floating = 2.5;
    for (int i = 0; i < 9; ++i) source.bytes[i] = (unsigned char)(i + 1);
    struct Payload destination; destination = source;
    const struct Payload frozen = destination;
    if (destination.integer != 17 || frozen.floating != 2.5) return 1;
    for (int i = 0; i < 9; ++i) if (frozen.bytes[i] != (unsigned char)(i + 1)) return 2;
    if (clone_arguments(8, 1, 2, 3, 4, 5, 6, 7, 8, 2.5) != 76) return 3;
    return 0;
}
