int f(int n) { if(n) goto done; return 3; done: return 7; } int main(void) { return f(0)!=3 || f(1)!=7; }
