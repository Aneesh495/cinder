
int main(void) { unsigned int x = 0x80000000U; long shift = 31; x >>= shift; return x != 1; }
