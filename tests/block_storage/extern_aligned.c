_Alignas(16) int x=5; int main(void) { _Alignas(16) extern int x; return (unsigned long)&x%16 || x!=5; }
