
int main(void) { unsigned int x = 0x80000000U; return (x >> 31) != 1U || (x << 1) != 0U; }
