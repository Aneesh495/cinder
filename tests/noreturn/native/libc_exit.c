_Noreturn void exit(int); _Noreturn void finish(int code) { exit(code); } int main(void) { finish(24); return 99; }
