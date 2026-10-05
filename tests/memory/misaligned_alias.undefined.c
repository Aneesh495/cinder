int main(void) { int a[3]; unsigned char *p = (unsigned char *)a; return *(int *)(p + 1); }
