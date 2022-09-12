struct S{union{struct{int x,y;} named;long packed;};};int main(void){struct S s={.named={3,5}};return s.named.x!=3||s.named.y!=5;}
