struct S{int n;int data[];};union U{struct S s;long extent;};int main(void){union U u={.s={3}};u.s.data[0]=71;return u.s.n!=3||u.s.data[0]!=71;}
