struct P { int x; int y; } a={1,2}; int calls;
struct P *target(void) { ++calls; return &a; }
int main(void) { struct P b={8,9}; *target()=b; return calls+a.x+b.x; }
