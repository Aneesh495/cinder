int first(int x) { return x+2; } int second(int x) { return x+4; } int (*callbacks[2])(int)={first,second}; int main(void) { return callbacks[0](5)+callbacks[1](7); }
