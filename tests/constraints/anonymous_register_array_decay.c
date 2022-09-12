struct S{struct{int a[2];};};int main(void){register struct S s={.a={7,11}};int *p=s.a;return *p;}
