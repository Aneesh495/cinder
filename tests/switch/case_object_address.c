int main(void) { int n=0; switch(3) { _Alignas(16) int a[2]={99,98}; case 3: { int *p=a; p[0]=4; p[1]=7; n=a[0]+a[1]; } } return n!=11; }
