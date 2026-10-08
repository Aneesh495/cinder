struct S{long n;char tag;char data[];};int main(void){struct S s={1,2};s.data[7]=3;return 0;}
