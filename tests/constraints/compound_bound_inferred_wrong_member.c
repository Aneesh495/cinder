struct P { int x; }; int main(void) { int a[sizeof (struct P[]){ {.absent=3} }]; return 0; }
