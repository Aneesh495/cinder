struct P { int x; }; int calls;
int main(void) { struct P a={5}; struct P b=(++calls,a); return b.x+calls; }
