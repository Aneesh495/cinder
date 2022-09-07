struct S{double n; char data[];};static double read(struct S s){return s.n;}int main(void){struct S s={3.25};return read(s)!=3.25;}
