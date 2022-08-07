struct P{_Alignas(16) double x;}; struct P f(struct P p){p.x+=2;return p;} int main(void){struct P (*p)(struct P)=f;struct P q=p((struct P){1.5});return q.x!=3.5 || sizeof q!=16;}
