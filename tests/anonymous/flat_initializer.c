struct S{struct{int x,y;};int z;};int main(void){struct S s={3,5,7};return s.x!=3||s.y!=5||s.z!=7;}
