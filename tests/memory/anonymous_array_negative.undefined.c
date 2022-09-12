struct S{struct{int a[2];};};int main(void){struct S s={.a={7,11}};return s.a[-1];}
