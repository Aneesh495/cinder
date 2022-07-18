struct S { int a,b; }; int main(void) { struct S *p=&(struct S){3,7}; p->b=11; return p->a+p->b != 14; }
