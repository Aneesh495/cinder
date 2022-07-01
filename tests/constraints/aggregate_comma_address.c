struct P { int n; };
int main(void) { struct P a={1}; struct P *p=&(0,a); return p->n; }
