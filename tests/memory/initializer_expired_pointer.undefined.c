int *get(void) { int a[1]={3}; return a; } int main(void) { struct P { int *p; }; struct P q={get()}; return *q.p; }
