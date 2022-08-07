int main(void) { _Alignas(32) int x=7; return (unsigned long)&x%32 || x!=7; }
