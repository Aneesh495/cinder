int main(void) { int a[3]={1,2,3}; int b[sizeof (0,a)]={4}; return sizeof b != sizeof(int*) * sizeof(int); }
