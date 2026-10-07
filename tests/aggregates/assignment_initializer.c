struct P { int x; int y; };
int main(void) { struct P a={1,2}, b={4,5}; struct P c=(a=b); return a.x+c.y; }
