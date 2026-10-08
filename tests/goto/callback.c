int f(void) { int n=0; again: if(++n<3) goto again; return n; } int apply(int (*f)(void)) { return f(); } int main(void) { return apply(f)!=3; }
