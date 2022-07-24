struct P { int x; }; int main(void) { int a[32]={[sizeof (struct P){.missing=3}]=1}; return 0; }
