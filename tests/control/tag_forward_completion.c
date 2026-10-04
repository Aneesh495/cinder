struct Node;
const struct Node *link;
struct Node { int value; struct Node *next; };
int main(void) { return sizeof(*link) != 16 || sizeof(struct Node) != 16; }
