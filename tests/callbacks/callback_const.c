int inc(int x) { return x + 1; } int main(void) { int (* const p)(int) = inc; return p(28); }
