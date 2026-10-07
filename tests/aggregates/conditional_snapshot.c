struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{7,3}}; int *p; int x=((p=(0?a:b).a),b.a[0]=9,*p); return x+b.a[0]; }
