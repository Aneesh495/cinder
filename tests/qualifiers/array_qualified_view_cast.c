struct S{int x;};int main(void){struct S s[2]={{3},{5}};const struct S *p=s;int *q=(int*)&p[1].x;*q=9;return s[0].x!=3||s[1].x!=9;}
