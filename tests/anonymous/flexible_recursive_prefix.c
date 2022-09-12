struct S{struct{union{int n;long wide;};};char data[];};int main(void){struct S s={.n=73};return s.n!=73||sizeof(s)!=8;}
