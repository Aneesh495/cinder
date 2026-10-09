int main(void){int x=13;int *p=&x;int a=*p;{int y;int *q=&y;*q=17;}int b=*p;return a+b;}
