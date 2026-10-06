int count(const char *p) { int n=0; while (p[n]) ++n; return n; } int main(void) { return count("a" "b" "c"); }
