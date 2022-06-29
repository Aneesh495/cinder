int first(int x) { return x+1; } int second(int x) { return x+3; } int main(void) { int (*callbacks[2])(int)={first,second}; return callbacks[0](5)+callbacks[1](7); }
