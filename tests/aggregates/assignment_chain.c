struct P { int x; int y; };
int main(void) { struct P a={1,2}, b={3,4}, c={6,7}; a=b=c; return a.x+b.x+c.x; }
