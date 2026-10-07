_Noreturn void exit(int); _Noreturn void finish(int code, ...) { exit(code); } int main(void) { finish(23,2.5,6L); return 99; }
