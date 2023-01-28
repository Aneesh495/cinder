volatile int material_gate = 1;
static int integer_copy(int x) { int y = x; return y; }
static double float_copy(double x) { double y = x; return y; }
static double negate(double x) { return -x; }
static float negate_single(float x) { return -x; }
static int initialized_integer(int c) { int value; if (c) value = 7; if (c) return value; return 0; }
static double initialized_float(int c) { double value; if (c) value = 3.5; if (c) return value; return 0.0; }
int main(void) {
    unsigned long wide = 0xfedcba9876543210UL;
    if (integer_copy(material_gate) != 1 || float_copy(2.5) != 2.5) return 1;
    if (negate(2.5) != -2.5 || negate(-3.0) != 3.0 || negate_single(1.25f) != -1.25f) return 2;
    if (initialized_integer(0) != 0 || initialized_integer(1) != 7) return 3;
    if (initialized_float(0) != 0.0 || initialized_float(1) != 3.5) return 4;
    if (wide != 0xfedcba9876543210UL) return 5;
    double zero = negate(0.0); float single_zero = negate_single(0.0f);
    unsigned char *bytes = (unsigned char *)&zero, *single = (unsigned char *)&single_zero;
    if (bytes[7] != 128 || single[3] != 128) return 6;
    return 0;
}
