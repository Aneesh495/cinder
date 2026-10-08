#include <stddef.h>
struct A { char c; _Alignas(16) max_align_t a; };
#if defined(__CINDER__) || defined(__x86_64__)
_Static_assert(_Alignof(max_align_t) == 16, "target maximum alignment");
#endif
int main(void) { return _Alignof(max_align_t) < _Alignof(double) || offsetof(struct A, a) != 16; }
