struct P { int x; } a={4}, b={7}; int calls;
struct P *left(void) { calls+=1; return &a; }
struct P *right(void) { calls+=20; return &b; }
int main(void) { struct P c=1?*left():*right(); return calls+c.x; }
