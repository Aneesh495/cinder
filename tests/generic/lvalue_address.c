int main(void) { int x=1,y=2; int *p=&_Generic(1,int:x,default:y); *p=7; return p!=&x || x!=7 || y!=2; }
