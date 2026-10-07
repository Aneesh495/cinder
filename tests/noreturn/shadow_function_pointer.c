_Noreturn void spin(void) { for (;;) {} } int normal(void) { return 7; } int main(void) { int (*spin)(void)=normal; return spin()!=7; }
