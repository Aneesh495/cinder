#include <stddef.h>
struct S{char lead;struct{double d;int n;};};_Static_assert(offsetof(struct S,d)==8&&offsetof(struct S,n)==16&&sizeof(struct S)==24,"physical nested offsets");int main(void){return 0;}
