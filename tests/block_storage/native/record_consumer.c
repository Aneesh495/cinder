struct P { int a; double b; }; int consume(void) { extern struct P value; static struct P *p=&value; ++p->a; return p->a+(int)p->b; } int main(void) { return consume()!=8 || consume()!=9; }
