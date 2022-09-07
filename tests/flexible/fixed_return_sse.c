struct S{double n; int data[];};static struct S make(double n){struct S s={n};return s;}int main(void){struct S s=make(6.5);return s.n!=6.5;}
