_Noreturn void exit(int); _Noreturn void finish(int code) { exit(code); } int main(void) { _Generic(1,int:finish)(17); return 99; }
