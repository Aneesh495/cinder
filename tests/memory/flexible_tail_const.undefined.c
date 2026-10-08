struct S{long n;char tag;const char data[];};int main(void){struct S s={1,2};char *p=(char *)s.data;p[0]=3;return 0;}
