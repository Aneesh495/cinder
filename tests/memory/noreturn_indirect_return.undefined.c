_Noreturn void f(void) { return; } int main(void) { void (*p)(void)=f; p(); return 0; }
