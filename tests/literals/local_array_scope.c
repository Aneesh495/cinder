int main(void) { int sum=0; for(int i=0;i<3;++i) { char a[]="ab"; a[0]+=i; sum+=a[0]-90; } return sum; }
