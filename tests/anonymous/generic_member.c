struct S{struct{int x;double d;};};int main(void){struct S s={.x=5,.d=2.5};return _Generic(s.d,double:0,default:1);}
