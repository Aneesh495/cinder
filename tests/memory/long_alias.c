int main(void) { long x = 0x123456780000001fL; long *p = &x; return (int)(*p & 255L); }
