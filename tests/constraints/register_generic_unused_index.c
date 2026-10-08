int main(void) { register int a[2]={3,7};return _Generic(1,int:0,default:a[0]); }
