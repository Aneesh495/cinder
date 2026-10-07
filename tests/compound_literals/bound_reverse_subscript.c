struct P { int a; int b; }; int main(void) { struct P p[2]={{1,2},{3,4}}; int v[sizeof (struct P[]){0[p],1[p]} / sizeof(struct P)]={1,2}; return sizeof v != 2 * sizeof(int); }
