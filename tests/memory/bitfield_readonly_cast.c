struct S { const unsigned a:3; unsigned b:5; };struct T { unsigned a:3; unsigned b:5; }; int main(void){struct S s={5,19};struct T *p=(struct T*)&s;p->a=3;return s.a;}
