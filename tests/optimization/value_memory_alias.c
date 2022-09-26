int work(int*p){int a=*p;*p=9;int b=*p;return a+b;}int main(void){int n=7;return work(&n);}
