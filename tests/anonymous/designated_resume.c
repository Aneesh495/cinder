struct S{struct{int x,y;};int z;};int main(void){struct S s={.x=19,23,29};return s.x!=19||s.y!=23||s.z!=29;}
