int main(void) { int *p=0; int n=0; again: { int x=3; if(n++==0) { p=&x; goto again; } return *p; } }
