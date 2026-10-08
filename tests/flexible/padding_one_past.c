struct S{long n; char tag; char data[];};int main(void){struct S s={1,2};char *a=s.data,*b=a+7;return b-a!=7;}
