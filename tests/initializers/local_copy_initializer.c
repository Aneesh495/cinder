struct P { int x; double y; }; int main(void) { struct P p={5,8.0}; struct P q=p; return q.x+(int)q.y; }
