int pick(register int n); int pick(int n) { int *p=&n; return *p; } int main(void) { return pick(7)!=7; }
