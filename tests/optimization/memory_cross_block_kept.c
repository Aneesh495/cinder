static volatile int flag=1;int main(void){int x=13;int *p=&x;int a=*p;if(flag)return a+*p;return a;}
