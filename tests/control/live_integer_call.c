long add(long a,long b) { return a+b; } long test(long x,long y) { return x * add(y,3) + x; } int main(void) { return test(4,5) != 36; }
