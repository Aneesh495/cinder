struct S{int n; char data[];};struct Holder{struct S *p;};int main(void){struct S s={37};struct Holder h={&s};return h.p->n!=37;}
