int main(void) {
    unsigned x = 0xf0U, y = 0x3cU;
    unsigned a = (x & y) | (x ^ y);
    unsigned b = ((a << 3) >> 2) + (~0U & 7U);
    int c = -17 / 4, d = -17 % 4;
    unsigned char narrow = 255;
    return a != 252U || b != 511U || c != -4 || d != -1 || narrow + 1 != 256;
}
