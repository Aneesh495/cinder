int main(void) { int x=1; int n=_Generic(1,int:++x,default:9); return n!=2 || x!=2; }
