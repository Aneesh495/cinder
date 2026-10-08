int a[3]={2,4,6}; int f(void) { extern int a[]; return a[1]+a[2]; } int main(void) { return f()!=10; }
