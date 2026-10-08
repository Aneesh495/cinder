int f(void) { static int x=3; return sizeof(x); } int main(void) { return f()!=4; }
