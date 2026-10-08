struct S{long n; int data[];};static long read(struct S s){return s.n;}int main(void){struct S s={31};return read(s)!=31;}
