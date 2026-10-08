struct S{int n; int mode; char data[];};int main(void){struct S s={.mode=7,.n=11};return s.n!=11||s.mode!=7;}
