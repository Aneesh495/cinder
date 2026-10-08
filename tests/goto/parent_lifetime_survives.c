int main(void) { int x=3; int *p=&x; { int n=2; if(n==2) goto finish; } finish: return p!=&x || *p!=3; }
