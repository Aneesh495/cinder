struct S{int n; char data[];};union U{struct S s; long v;};int main(void){union U u={.s={23}};return u.s.n!=23||sizeof(union U)!=8;}
