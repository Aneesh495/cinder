struct P { const int x; int y; }; int main(void) { struct P p={1,2}; int *q=(int *)&p.x; *q=3; return p.x; }
