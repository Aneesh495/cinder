struct Triple { long a, b, c; };
struct Envelope { char prefix[65536]; struct Triple item; };
struct Triple global_values[4] = {{2,3,5}, {7,11,13}, {17,19,23}, {29,31,37}};
static struct Envelope envelope = {.item = {41,43,47}};
int callback(int value) { return value + 7; }

int main(void) {
    struct Triple local[4] = {{2,3,5}, {7,11,13}, {17,19,23}, {29,31,37}};
    struct Triple *left = local + 1;
    struct Triple *right = left + 2;
    struct Triple *back = right - 2;
    struct Triple *first = left + -1;
    long difference = right - left;
    long reverse = left - right;
    int (*function)(int) = callback;
    char *last = envelope.prefix + 65535;
    *last = 5;
    return (int)back->b + (int)right->c + (int)global_values[2].a
           + (int)envelope.item.c + *last + function(1)
           + (int)(difference - reverse) + (int)first->a;
}
