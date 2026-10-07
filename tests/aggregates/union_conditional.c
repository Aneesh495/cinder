union U { int n; double d; };
int main(void) { union U a={6}, b={9}; union U c=0?a:b; return c.n; }
