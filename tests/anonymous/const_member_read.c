struct S{const struct{int n;};int x;};int main(void){struct S s={.n=29,.x=3};s.x+=2;return s.n!=29||s.x!=5;}
