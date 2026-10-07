struct P { int n; };
int main(void) { struct P a={1}, b={2}; (1?a:b).n=3; return a.n; }
