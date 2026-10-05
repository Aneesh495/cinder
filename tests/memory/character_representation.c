int main(void) { unsigned int x = 0x01020304U; unsigned char *p = (unsigned char *)&x; p[0] = 20; return x & 255U; }
