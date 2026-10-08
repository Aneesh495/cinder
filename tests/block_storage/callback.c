int next(void) { static int n; return ++n; } int twice(int (*f)(void)) { return f()+f(); } int main(void) { return twice(next)!=3 || next()!=3; }
