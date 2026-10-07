struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{3,4}}; int *p=(a=b).a; return *p; }
