int x; int main(void) { int x=3; static int *p=&x; return *p; }
