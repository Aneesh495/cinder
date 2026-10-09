int main(void){volatile int x=0;volatile int *p=&x;*p=13;*p=13;return x;}
