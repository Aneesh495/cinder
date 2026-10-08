struct S{union{int n;double weight;};char data[];};int main(void){struct S s={.n=71};return s.n!=71||sizeof(s)!=8;}
