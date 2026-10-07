struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{2,3}}; int *p; int x=((p=(a=b).a),a.a[0]=9,*p); return x+a.a[0]; }
