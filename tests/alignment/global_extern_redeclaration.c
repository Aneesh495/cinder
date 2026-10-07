_Alignas(16) extern int x; _Alignas(16) int x=7; extern int x; int main(void){return (unsigned long)&x%16 || x!=7;}
