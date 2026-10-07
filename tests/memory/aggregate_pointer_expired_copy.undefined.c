struct P { int *p; }; struct P a;
int main(void) { { int x=7; a.p=&x; } struct P b=a; return *b.p; }
