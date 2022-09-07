struct S{long n; int mode; char data[];};static struct S s;int main(void){return s.n!=0||s.mode!=0;}
