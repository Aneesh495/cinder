struct P { int *p; int n; };
int main(void) { int x=8, y=2; struct P a={&y,1}, b={&x,3}; a=b; return *a.p+a.n; }
