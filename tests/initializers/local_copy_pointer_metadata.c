struct P { int *x; int y; }; int main(void) { int n=12; struct P p={&n,9}; struct P q=p; return *q.x+q.y; }
