struct P { int x; int y; }; int main(void) { struct P p={8,9}; const struct P q=p; return q.x+q.y; }
