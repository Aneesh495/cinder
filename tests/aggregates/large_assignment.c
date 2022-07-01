struct P { double d; int a[5]; int *p; };
int main(void) { int x=9; struct P a={1.0,{1,2,3,4,5},0}, b={3.0,{6,7,8,9,10},&x}; a=b; return (int)a.d+a.a[4]+*a.p; }
