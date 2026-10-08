struct S{struct{int a[3];};};int main(void){struct S s={.a={7,11,13}};return s.a[0]+s.a[2]!=20;}
