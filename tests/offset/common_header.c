#include <stddef.h>
#include <stdint.h>
int main(void) { int a[3]; ptrdiff_t d = &a[2] - &a[0]; wchar_t w = 65; size_t z = sizeof(a); return d != 2 || w != 65 || z != 12 || NULL != (void *)0 || sizeof(uintptr_t) != sizeof(void *); }
