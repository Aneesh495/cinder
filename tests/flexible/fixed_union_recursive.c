struct S{int n; char data[];};union U{struct S s; long v;};union V{union U u; double d;};int main(void){union V v={.u={.s={29}}};return v.u.s.n!=29;}
