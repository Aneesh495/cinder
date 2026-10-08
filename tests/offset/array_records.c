#include <stddef.h>
struct Cell { char c; int n; };
struct Board { short flag; struct Cell cells[4]; };
int main(void) { return offsetof(struct Board, cells[3].n) != 32; }
