struct S { unsigned a:4; unsigned b:4; }; int main(void){struct S s[2]={{3,5},{7,9}};int i=0;int r=s[i++].a++;return i!=1 || r!=3 || s[0].a!=4 || s[0].b!=5 || s[1].a!=7;}
