int main(void) { int a[3]={1,2,3}; return _Generic(&a,int(*)[3]:1,int*:2,default:3)-1; }
