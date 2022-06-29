int inc(int x) { return x+5; } struct P { int (*f)(int); int x; }; int main(void) { struct P p={inc,7}; struct P q=p; return q.f(q.x); }
