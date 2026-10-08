struct S { volatile unsigned a:3; volatile signed b:5; }; int main(void){struct S s={5,-7};int old=s.a++;int now=++s.b;return old!=5 || s.a!=6 || now!=-6 || s.b!=-6;}
