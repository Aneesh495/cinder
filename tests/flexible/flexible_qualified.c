struct S{long n;const char data[];};int main(void){struct S s={43};const char *p=s.data;return s.n!=43||p==0;}
