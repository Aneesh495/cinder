struct S{int n;char data[];};int main(void){struct S *p=&(struct S){59};return p->n!=59;}
