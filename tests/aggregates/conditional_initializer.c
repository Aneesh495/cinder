struct P { int x; int y; };
int main(void) { struct P a={2,3}, b={5,6}; struct P c=1?a:b; return c.x+c.y; }
