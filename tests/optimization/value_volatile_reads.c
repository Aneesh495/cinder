int work(void){volatile int n=7;int a=n;int b=n;return a+b;}int main(void){return work();}
