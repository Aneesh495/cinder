struct P{_Alignas(16) int x;}; struct P f(struct P p){p.x+=2;return p;} int main(void){struct P p=f((struct P){7});return p.x!=9 || sizeof p!=16 || _Alignof(struct P)!=16;}
