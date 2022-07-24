struct P { int a[2]; int b; }; int main(void) { int v[sizeof (struct P[]){[2].a[1]=3,4,5,6,7} / sizeof(struct P)] = {[3]=9}; return sizeof v != 4 * sizeof(int) || v[3] != 9; }
