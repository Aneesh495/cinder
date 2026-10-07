struct P { int x; int y; };
int main(void) { struct P a={2,3}, b={5,6}; return (0?a:b).y; }
