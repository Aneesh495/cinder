_Noreturn void spin(void) { for (;;) {} } void (*p)(void)=spin; int main(void) { return p!=spin; }
