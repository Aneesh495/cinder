static _Noreturn void spin(void) { for (;;) {} } int main(void) { void (*p)(void)=spin; return p==0; }
