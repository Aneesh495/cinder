#include <stddef.h>
union U { int count; double real; char data[16]; };
int main(void) { return offsetof(union U, count) || offsetof(union U, real) || offsetof(union U, data[7]) != 7; }
