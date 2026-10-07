struct P{char a; _Alignas(0) int x;}; int main(void){struct P p={3,7}; return _Alignof(struct P)!=4 || sizeof p!=8 || (unsigned long)&p.x-(unsigned long)&p!=4;}
