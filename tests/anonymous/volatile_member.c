struct S{struct{int n;};};int main(void){volatile struct S s={.n=23};++s.n;return s.n!=24;}
