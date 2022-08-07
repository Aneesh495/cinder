int _Alignas(16) a=7; int main(void){const _Alignas(16) int x=9; return (unsigned long)&x%16 || (unsigned long)&a%16 || x!=9 || a!=7;}
