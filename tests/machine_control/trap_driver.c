_Noreturn void exit(int status);
_Noreturn void stop(void) { exit(77); }
int guarded(int n);
int main(int argc, char **argv) { (void)argv; return guarded(argc == 1 ? 3 : -1) == 4 ? 0 : 1; }
