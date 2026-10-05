int *identity(int *p) { return p; } int main(void) { int x = 29; int *(*fn)(int *) = identity; return *fn(&x); }
