int *address(void) { static const int x=3; return (int *)&x; } int main(void) { *address()=7; return 0; }
