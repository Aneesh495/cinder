struct P{_Alignas(16) int x;}; int main(void){struct P *p=&(struct P){7};return (unsigned long)p%16 || p->x!=7 || _Alignof(struct P)!=16;}
