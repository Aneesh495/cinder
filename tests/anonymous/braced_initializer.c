struct S{struct{int x,y;};int z;};int main(void){struct S s={{11,13},17};return s.x!=11||s.y!=13||s.z!=17;}
