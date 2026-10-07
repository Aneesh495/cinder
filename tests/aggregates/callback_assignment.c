int f(int x) { return x+7; }
struct P { int (*f)(int); int n; };
int main(void) { struct P a={0,1}, b={f,3}; a=b; return a.f(a.n); }
