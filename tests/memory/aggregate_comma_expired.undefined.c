struct P { int a[2]; };
int main(void) { struct P a={{1,2}}; int *p=(0,a).a; return *p; }
