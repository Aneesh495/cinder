int main(void) { unsigned int x = 0xffffffffU; unsigned int *p = &x; *p += 20; return x; }
