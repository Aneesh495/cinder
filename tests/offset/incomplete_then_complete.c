#include <stddef.h>
struct Forward;
typedef struct Forward Node;
struct Forward { int key; Node *next; };
int main(void) { return offsetof(Node, next) != 8; }
