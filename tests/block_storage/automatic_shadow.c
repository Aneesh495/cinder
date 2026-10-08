int f(void) { static int x=3; int a=x++; { int x=10; a+=x; } return a; } int main(void) { return f()!=13 || f()!=14; }
