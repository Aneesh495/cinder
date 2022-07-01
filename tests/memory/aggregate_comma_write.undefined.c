struct P { int a[2]; };
int main(void) { struct P a={{1,2}}; return (0,a).a[0]=9; }
