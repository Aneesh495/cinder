int f(void) { static int x=3; int a=x++; { static int x=9; a+=x++; } return a; } int main(void) { return f()!=12 || f()!=14; }
