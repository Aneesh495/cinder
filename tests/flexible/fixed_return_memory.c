struct S{long a,b,c; char data[];};static struct S make(void){struct S s={7,11,13};return s;}int main(void){struct S s=make();return s.a!=7||s.b!=11||s.c!=13;}
