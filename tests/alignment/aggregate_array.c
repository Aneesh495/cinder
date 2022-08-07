struct P{_Alignas(16) char x;}; int main(void){struct P p[3]={{1},{2},{3}}; return _Alignof(struct P[3])!=16 || sizeof p!=48 || (unsigned long)&p[1]-(unsigned long)&p[0]!=16 || p[2].x!=3;}
