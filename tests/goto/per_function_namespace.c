int f(void) { goto finish; finish: return 3; } int g(void) { goto finish; finish: return 7; } int main(void) { return f()!=3 || g()!=7; }
