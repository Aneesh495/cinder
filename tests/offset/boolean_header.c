#include <stdbool.h>
#if !__bool_true_false_are_defined
#error missing boolean macro
#endif
bool choose(bool a, bool b) { return a || b; }
int main(void) { bool a = true; bool b = false; return choose(a, b) != 1 || b != 0; }
