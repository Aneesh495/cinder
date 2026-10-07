struct P { int x; int y; };
int main(void) { struct P a={3,4}; a=a; return a.x+a.y; }
