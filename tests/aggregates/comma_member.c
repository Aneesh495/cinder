struct P { int x; }; int calls;
int main(void) { struct P a={3}; int x=(++calls,a).x; return x+calls; }
