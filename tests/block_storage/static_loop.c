int f(void) { int sum=0; for(int i=0;i<4;++i) { static int x=3; sum+=++x; } return sum; } int main(void) { return f()!=22 || f()!=38; }
