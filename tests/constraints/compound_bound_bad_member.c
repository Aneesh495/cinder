struct P { int x; }; int main(void) { int a[sizeof (struct P){.missing=3}]; return 0; }
