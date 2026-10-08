int f(void) { static int a[3]={1,2,3}; a[1]+=2; return a[0]+a[1]+a[2]; } int main(void) { return f()!=8 || f()!=10; }
