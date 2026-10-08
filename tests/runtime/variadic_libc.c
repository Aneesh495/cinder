#include <stdio.h>
#include <stdarg.h>
#include <string.h>
int format(char *output, unsigned long size, const char *pattern, ...) { va_list a, b; va_start(a, pattern); va_copy(b, a); int n = vsnprintf(output, size, pattern, b); va_end(b); va_end(a); return n; }
int main(void) { char text[64]; int n = format(text, sizeof(text), "%d:%ld:%.2f", 19, 12345678901L, 2.75); return n != 19 || strcmp(text, "19:12345678901:2.75"); }
