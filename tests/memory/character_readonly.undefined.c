int main(void) { const int x = 7; unsigned char *p = (unsigned char *)&x; p[0] = 8; return x; }
