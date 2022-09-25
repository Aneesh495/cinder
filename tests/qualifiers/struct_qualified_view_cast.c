struct S{int x;int y;};int main(void){struct S s={3,5};const struct S *p=&s;int *q=(int*)&p->y;*q=9;return s.x!=3||s.y!=9;}
