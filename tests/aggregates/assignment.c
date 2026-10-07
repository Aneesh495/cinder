struct P { int x; int y; };
int main(void) { struct P a={1,2}, b={4,5}; a=b; return a.x+a.y; }
