_Noreturn void spin(void) { for (;;) {} } int main(void) { return _Generic(&spin, void (*)(void):1, default:0)!=1; }
