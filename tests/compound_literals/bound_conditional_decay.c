int main(void) { int a[3]={1,2,3}; int b[sizeof (1 ? a : a)]={4}; return sizeof b != sizeof(int*) * sizeof(int); }
