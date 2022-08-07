struct P{char a; _Alignas(16) int x;}; int main(void){struct P p={3,7},q=p; q.x=9; return sizeof q!=32 || q.a!=3 || q.x!=9 || p.x!=7;}
