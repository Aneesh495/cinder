struct P { int x; }; int main(void) { struct P p={1}; int a[sizeof (int){-p}]; return 0; }
