int main(void) { int a[2]={3,7}; register int *p=a; p++; return *p!=7; }
