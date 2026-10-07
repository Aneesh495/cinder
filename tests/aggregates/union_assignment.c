union U { int n; double d; };
int main(void) { union U a={1}, b={17}; a=b; return a.n; }
