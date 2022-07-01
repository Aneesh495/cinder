struct P { int x; int y; }; struct Q { struct P p[2]; };
int main(void) { struct P a={3,4}, b={6,7}; struct Q q={{1?a:b,0?a:b}}; return q.p[0].x+q.p[1].y; }
