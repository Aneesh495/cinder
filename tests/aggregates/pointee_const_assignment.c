struct P { const int *p; int x; };
int main(void) { const int a=6; struct P p={0,1}, q={&a,4}; p=q; return *p.p+p.x; }
