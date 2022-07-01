struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{3,4}}; int *p; do {} while ((p=(0?a:b).a,0)); return *p; }
