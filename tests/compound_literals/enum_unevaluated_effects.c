int calls; int inc(void) { ++calls; return 3; } int main(void) { enum { N=sizeof (int[]){inc(),inc(),inc()} / sizeof(int) }; return calls || N != 3; }
