int main(void){int x=0;int *p=&x;int a=*p;char *q=(char *)p;*q=7;int b=*p;return a+b;}
