int main(void) { int a[2]={3,7}; register int *p=a; int *q=&p[1]; return q!=a+1 || *q!=7; }
