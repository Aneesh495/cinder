int next(void) { static int n=7; return n++; } int main(void) { return next()!=7 || next()!=8; }
