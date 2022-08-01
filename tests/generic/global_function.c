int f(int x) { return x+1; } int g(int x) { return x-1; } int (*p)(int)=_Generic(1,int:f,default:g); int main(void) { return p(7)-8; }
