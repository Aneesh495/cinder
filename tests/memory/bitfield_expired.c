struct S { unsigned a:3; }; int main(void){struct S *p;{struct S s={5};p=&s;}return p->a;}
