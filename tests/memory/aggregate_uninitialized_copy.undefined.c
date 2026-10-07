struct P { int x; int y; };
int main(void) { struct P a; struct P b=a; return b.x; }
