int main(void) { int a[3]={1,2,3}; return _Generic(a,int*:1,int[3]:2,default:3)-1; }
