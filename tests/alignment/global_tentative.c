_Alignas(16) int x; _Alignas(16) int x; int main(void){x=9; return (unsigned long)&x%16 || x!=9;}
