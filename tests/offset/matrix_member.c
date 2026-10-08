#include <stddef.h>
struct Grid { int kind; long cells[3][5]; };
int main(void) { return offsetof(struct Grid, cells[2][4]) != 120; }
