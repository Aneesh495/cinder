int count(const char *p) { int n=0; while(p[n]) ++n; return n; } int main(void) { char a[]="abc"; int (*fn)(const char *)=count; return fn(a); }
