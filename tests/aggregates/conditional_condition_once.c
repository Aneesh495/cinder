struct P { int x; };
int calls; int choose(void) { ++calls; return 0; }
int main(void) { struct P a={4}, b={7}; int x=(choose()?a:b).x; return x+calls; }
