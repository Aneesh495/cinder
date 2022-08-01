int main(void) { int x=1,y=2; int n=_Generic(1,int:x,default:y)++; return n!=1 || x!=2 || y!=2; }
