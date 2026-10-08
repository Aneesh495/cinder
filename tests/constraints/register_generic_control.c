int main(void) { register int a[2]={3,7};return _Generic(a,int *:0,default:1); }
