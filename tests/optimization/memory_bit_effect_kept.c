struct S{unsigned bits:3;int value;};int main(void){struct S s={1,13};int *p=&s.value;int a=*p;s.bits=2;int b=*p;return a+b;}
