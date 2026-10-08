struct S{struct{int a[3];};int x;};int main(void){struct S s={.a[2]=41,.x=43};return s.a[0]!=0||s.a[2]!=41||s.x!=43;}
