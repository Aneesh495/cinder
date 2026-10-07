struct P { int a[2]; };
int main(void) { struct P a={{5,2}}; int *p; int x=((p=(0,a).a),a.a[0]=9,*p); return x+a.a[0]; }
