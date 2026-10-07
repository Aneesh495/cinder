struct P { int x; }; int main(void) { enum { N=sizeof (struct P){.absent=3} }; return 0; }
