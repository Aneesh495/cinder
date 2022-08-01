int main(void) { int x=3; int n=_Generic(++x,int:7,default:9); return x!=3 || n!=7; }
