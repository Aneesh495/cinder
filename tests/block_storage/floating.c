double f(void) { static double n=0.5; n+=0.25; return n; } int main(void) { return f()!=0.75 || f()!=1.0; }
