int x=7,y=9; int *p=&_Generic(1,int:x,default:y); int main(void) { return p!=&x || *p!=7; }
