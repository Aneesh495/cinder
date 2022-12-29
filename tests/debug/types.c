enum State { STATE_NEGATIVE = -4, STATE_READY = 7, STATE_ALIAS = 7 };
struct Node;
struct Opaque;
union Bits { double real; unsigned long word; };
struct Payload {
    char tag;
    union Bits item;
    unsigned flag : 3;
    signed code : 5;
    unsigned : 0;
    int values[3];
    struct Node *next;
    struct Opaque *opaque;
};
struct Node { struct Payload payload; struct Node *next; };
struct Tail { int count; char bytes[]; };
typedef int (*Callback)(const struct Node *restrict node, int count, ...);

double debug_types(volatile struct Node *restrict node, enum State state,
                   Callback callback, unsigned char byte, float fraction,
                   struct Tail *tail) {
    (void)callback;
    (void)tail;
    byte = (unsigned char)(byte + 1U);
    return node->payload.item.real + node->payload.values[1] + state + byte + fraction;
}

int main(void) {
    struct Node node = { .payload = { .item = { .real = 1.5 }, .values = { 0, 3, 0 } } };
    return (int)debug_types(&node, STATE_READY, 0, 2, 0.5f, 0);
}
