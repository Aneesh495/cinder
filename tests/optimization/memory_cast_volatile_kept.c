int main(void){volatile int x=13;int *p=(int *)&x;int a=*p,b=*p;return a+b;}
