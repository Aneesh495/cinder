struct S{long n; char data[];};static struct S make(long n){struct S s={n};return s;}int main(void){struct S s=make(53);return s.n!=53;}
