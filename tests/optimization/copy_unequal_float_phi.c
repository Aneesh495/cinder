static volatile int flag=0;static volatile double input=2.5;int main(void){double x=input,r;if(flag)r=x;else r=x+0.5;return (int)(r*2.0);}
