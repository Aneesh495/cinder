#include "parameters.h"

double debug_stack(int a, int b, int c, int d, int e, int f, int g,
                   const int *p, double x0, double x1, double x2,
                   double x3, double x4, double x5, double x6,
                   double x7, double x8) {
    int witness = debug_stack_call(g);
    return a + b + c + d + e + f + witness + *p
           + x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8;
}
