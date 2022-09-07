struct S{int n;int data[];};int main(void){struct S s={1};int *p=s.data+1;return p==0;}
