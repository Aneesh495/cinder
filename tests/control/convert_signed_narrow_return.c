signed char narrow(int x) { return x; }
int main(void) { return narrow(-128) != -128; }
