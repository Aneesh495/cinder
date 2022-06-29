enum E { AT=2 }; int main(void) { int a[5] = {[AT+1]=17, [1]=19}; return a[1]+a[3]+a[4]; }
