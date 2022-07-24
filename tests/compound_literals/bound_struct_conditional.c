struct P { int x; int y; }; int main(void) { struct P p={1,2},q={3,4}; int v[sizeof (struct P[]){1 ? p : q,p} / sizeof(struct P)] = {1,2}; return sizeof v != 2 * sizeof(int); }
