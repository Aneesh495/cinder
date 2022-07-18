struct S { int *p; long value; }; struct S *p=&(struct S){.p=&(int){37},.value=5}; int main(void) { return *p->p+p->value!=42; }
