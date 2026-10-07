_Noreturn void exit(int); _Noreturn void first(int); _Noreturn void second(int x) { first(x+1); } void first(int x) { exit(x); } int main(void) { second(30); return 99; }
