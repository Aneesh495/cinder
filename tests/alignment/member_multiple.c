struct P{char a; _Alignas(1) _Alignas(8) int x;}; int main(void){struct P p={3,7}; return _Alignof(struct P)!=8 || sizeof p!=16 || (unsigned long)&p.x-(unsigned long)&p!=8 || p.x!=7;}
