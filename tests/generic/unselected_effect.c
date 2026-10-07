int main(void) { int x=1; int n=_Generic(x,int:7,default:++x); return n!=7 || x!=1; }
