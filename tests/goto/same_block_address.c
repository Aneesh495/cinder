int main(void) { int n=0; int x=3; int *p=&x; again: ++x; if(n++==0) goto again; return p!=&x || *p!=5; }
