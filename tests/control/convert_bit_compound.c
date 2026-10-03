
int main(void) { unsigned short x = 65535; x ^= 0xFF00; x &= 7; x |= 16; return x != 23; }
