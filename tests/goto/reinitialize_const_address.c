int main(void) { int n=0; const int *p=0; again: ; const int x=3; if(n++==0) { p=&x; goto again; } return p!=&x || *p!=3; }
