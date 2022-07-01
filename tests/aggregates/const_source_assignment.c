struct P { int x; int y; };
int main(void) { const struct P a={4,5}; struct P b={1,2}; b=a; return b.x+b.y; }
