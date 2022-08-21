int main(void) { int n=0; again: ; static int x=3; ++x; if(n++==0) goto again; return x!=5; }
