#include <stdbool.h>
#undef bool
#undef true
#undef false
#define bool int
#define true 17
#define false -3
int main(void) { bool a = true; bool b = false; return a != 17 || b != -3; }
