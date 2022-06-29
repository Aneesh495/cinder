int n; struct P { _Bool a; _Bool b; _Bool c; }; struct P p={&n, (void *)0, (_Bool)&n}; int main(void) { return p.a+2*p.c+p.b; }
