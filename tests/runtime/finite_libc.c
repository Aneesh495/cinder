#include <math.h>
#include <float.h>
#include <string.h>
int main(void) { unsigned char infinity[8] = {0, 0, 0, 0, 0, 0, 240, 127}; unsigned char nan[8] = {1, 0, 0, 0, 0, 0, 240, 127}; double a, b; memcpy(&a, infinity, 8); memcpy(&b, nan, 8); return !isfinite(0.0) || !isfinite(-0.0) || !isfinite(DBL_MAX) || !isfinite(DBL_MIN) || !isfinite(DBL_TRUE_MIN) || !isfinite(FLT_MAX) || isfinite(a) || isfinite(b); }
