struct P { int x; int y; };
int main(void) { struct P a={1,2}, b={9,3}; return (a=b).x; }
