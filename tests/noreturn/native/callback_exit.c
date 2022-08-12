_Noreturn void exit(int); _Noreturn void finish(int code) { exit(code); } int main(void) { void (*p)(int)=finish; p(42); return 99; }
