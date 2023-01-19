#include <stdarg.h>

static int observed;
static void touch(int n) { observed += n; }
static int choose(int n) { if (n < 0) return -3; if (n == 0) return 4; return n + 7; }
static float single(int n) { if (n) return 1.25f; return -2.5f; }
static double recurse(int n) { if (n == 0) return 0.5; return recurse(n - 1) + 1.0; }
static int rotate(int n) {
    int a = 1, b = 2, c = 3;
    double x = 1.0, y = 2.0, z = 3.0;
    for (int i = 0; i < n; ++i) { int t = a; a = b; b = c; c = t; double f = x; x = y; y = z; z = f; }
    return a * 9 + b * 3 + c + (int)(x * 9.0 + y * 3.0 + z);
}
static int variadic(int n, ...) {
    va_list args; va_start(args, n); int total = 0;
    for (int i = 0; i < n; ++i) total += va_arg(args, int);
    double tail = va_arg(args, double); va_end(args); return total + (int)tail;
}
static int large(int n) { char bytes[16384]; bytes[0] = 3; bytes[16383] = 5; return bytes[0] + bytes[16383] + n; }
int main(void) {
    touch(3); touch(4);
    if (observed != 7 || choose(-1) != -3 || choose(0) != 4 || choose(9) != 16) return 1;
    if (single(1) != 1.25f || single(0) != -2.5f || recurse(5) != 5.5) return 2;
    if (rotate(5) != 64 || variadic(8, 1, 2, 3, 4, 5, 6, 7, 8, 2.5) != 38) return 3;
    if (large(9) != 17) return 4;
    return 0;
}
