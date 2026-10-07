struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{3,4}}; int *p; return ((p=(1?a:b).a),*p=9); }
