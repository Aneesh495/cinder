int f(void) { return 3; } int main(void) { static int x[2]={f(),2}; return x[0]; }
