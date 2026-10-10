#include <stdint.h>
static int read_word(uintptr_t word, uintptr_t zero) { return *(int *)(void *)(word + zero); }
int main(void) { int value = 17; return read_word((uintptr_t)(void *)&value, 0); }
