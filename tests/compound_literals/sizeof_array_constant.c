int a[sizeof (int[3]){1,2,3}/sizeof(int)]; int main(void) { return sizeof(a)!=12; }
