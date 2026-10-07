int x=7; _Alignas(16) int *p=&x; int main(void){return (unsigned long)&p%16 || p!=&x || *p!=7;}
