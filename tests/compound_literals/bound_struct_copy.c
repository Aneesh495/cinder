struct P { int x; int y; }; int main(void) { const struct P p={1,2}; int v[sizeof (struct P[]){p,p} / sizeof(struct P)] = {3,4}; return sizeof v != 2 * sizeof(int) || v[1] != 4; }
