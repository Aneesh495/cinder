struct S{union{struct{int x,y;};long packed;};int z;};int main(void){struct S s={.x=7,.y=11,.z=13};return s.x+s.y+s.z!=31;}
