int main(void) { register int n=3; int v=0; { int n=7;int *p=&n;v=*p; } return n!=3 || v!=7; }
