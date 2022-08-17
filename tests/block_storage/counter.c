int next(void) { static int n; return ++n; } int main(void) { return next()!=1 || next()!=2 || next()!=3; }
