#include <stdlib.h>
int compare(const void *a, const void *b) { int x = *(const int *)a; int y = *(const int *)b; return (x > y) - (x < y); }
int main(void) { int values[9] = { 71, -3, 18, 0, -72, 18, 5, 100, -1 }; qsort(values, 9, sizeof(values[0]), compare); for (int i = 1; i < 9; ++i) if (values[i-1] > values[i]) return 1; return values[0] != -72 || values[8] != 100 || values[5] != 18; }
