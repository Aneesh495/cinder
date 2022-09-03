#include <stdlib.h>
#include <errno.h>
#include <limits.h>
int main(void) { char *end; long a = strtol("-317x", &end, 10); if (a != -317 || *end != 'x') return 1; double b = strtod("0x1.4p3!", &end); if (b != 10.0 || *end != '!') return 2; float c = strtof("1.25;", &end); if (c != 1.25F || *end != ';') return 3; errno = 0; unsigned long long d = strtoull("18446744073709551616", &end, 10); return d != ULLONG_MAX || errno != ERANGE || *end != 0; }
