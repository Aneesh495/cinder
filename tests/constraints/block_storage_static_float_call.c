double f(void) { return 3.0; } int main(void) { static double x=f(); return (int)x; }
