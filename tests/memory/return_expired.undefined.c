int *bad(void) { int x = 17; return &x; } int main(void) { return *bad(); }
