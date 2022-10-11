static volatile int flag=1;static volatile double input=2.5;int main(void){double x=input,r;if(flag)r=x;else r=x;return (int)(r*2.0);}
