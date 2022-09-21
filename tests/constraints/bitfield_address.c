struct S { unsigned n:3; }; int main(void){struct S s={1};unsigned *p=&s.n;return *p;}
