int main(void) { const int x=3; const int *p=&x; return _Generic(p,int*:1,const int*:2,default:3)-2; }
