struct S{int n; int mode; char data[];};int main(void){struct S a={3,5},b={7,11};b=a;return b.n!=3||b.mode!=5;}
