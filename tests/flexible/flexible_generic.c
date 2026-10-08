struct S{int n;double data[];};int main(void){struct S s={47};return _Generic(s.data,double *:0,default:1);}
