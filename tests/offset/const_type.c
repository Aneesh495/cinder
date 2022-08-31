#include <stddef.h>
struct Item { short key; long count; };
typedef const struct Item ConstItem;
int main(void) { return offsetof(ConstItem, count) != 8; }
