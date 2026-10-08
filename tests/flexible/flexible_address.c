struct S{int n; char data[];};int main(void){struct S s={1};char *p=s.data;return p!=&s.data[0];}
