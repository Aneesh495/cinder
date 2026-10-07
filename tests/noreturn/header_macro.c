#include <stdnoreturn.h>
noreturn void spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
