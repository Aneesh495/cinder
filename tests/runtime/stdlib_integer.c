#include <limits.h>
#include <stdlib.h>

_Static_assert(_Generic(&strtoul, unsigned long (*)(const char *, char **, int): 1, default: 0), "strtoul prototype");

int main(void) {
    unsigned long (*parse)(const char *, char **, int) = strtoul;
    char hexadecimal[] = " \t+0x2afrest", octal[] = "0377tail", negative[] = "-1x", invalid[] = "no";
    char *end = NULL;
    if (parse(hexadecimal, &end, 0) != 687UL || end != hexadecimal + 8 || *end != 'r') return 1;
    if (strtoul(octal, &end, 0) != 255UL || end != octal + 4) return 2;
    if (strtoul(negative, &end, 10) != ULONG_MAX || end != negative + 2) return 3;
    if (strtoul(invalid, &end, 10) != 0UL || end != invalid) return 4;
    if (parse("10101", NULL, 2) != 21UL || strtoul("z9!", &end, 36) != 1269UL || *end != '!') return 5;
    if (strtoul("18446744073709551615", &end, 10) != ULONG_MAX || *end != 0) return 6;
    return 0;
}
