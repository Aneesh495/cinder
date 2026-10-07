char before; _Alignas(16) char x; int main(void){x=9; return (unsigned long)&x%16 || x!=9 || before!=0;}
