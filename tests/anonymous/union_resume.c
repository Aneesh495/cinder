struct S{union{int x;long y;};int z;};int main(void){struct S s={.x=31,37};return s.x!=31||s.z!=37;}
