int main(void){volatile int x=13;volatile int *p=&x;int a=*p,b=*p;return a+b;}
