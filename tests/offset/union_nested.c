#include <stddef.h>
struct Pair { int x; double y; };
union Choice { struct Pair pair; char bytes[16]; };
struct Packet { long id; union Choice choice; };
int main(void) { return offsetof(struct Packet, choice.pair.y) != 16; }
