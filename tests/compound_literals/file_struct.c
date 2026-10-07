struct S { int a; double b; }; struct S *p=&(struct S){.b=2.5,.a=7}; int main(void) { p->a+=2; return p->a!=9 || p->b!=2.5; }
