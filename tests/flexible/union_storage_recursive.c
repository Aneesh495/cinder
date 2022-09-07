struct S{int n;int data[];};union U{struct S s;long extent;};union V{union U u;long long extent;};int main(void){union V v={.u={.s={7}}};v.u.s.data[0]=73;return v.u.s.n!=7||v.u.s.data[0]!=73;}
