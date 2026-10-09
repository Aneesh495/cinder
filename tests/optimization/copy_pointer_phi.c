static volatile int flag=1; int main(void){int a[2]={7,9};int *p=a,*q;if(flag)q=p;else q=p;return q[1];}
