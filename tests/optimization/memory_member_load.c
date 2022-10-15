struct S{int x,y;};int main(void){struct S s={13,17};int *p=&s.x;int a=*p,b=*p;return a+b;}
