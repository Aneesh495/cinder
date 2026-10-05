struct Handler { int (*fn)(int); int value; }; int add(int x) { return x + 12; } int main(void) { struct Handler h; h.fn = add; h.value = 25; return h.fn(h.value); }
