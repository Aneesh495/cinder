struct S{struct{int x;};};int main(void){struct S s={.x=7};_Generic(1,int:s.x)=11;return s.x!=11;}
