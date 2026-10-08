struct S { signed a:32; }; int main(void){struct S s={-2147483647-1};return s.a!=(-2147483647-1);}
