enum E { N=_Generic(1,int:3,default:5) }; int main(void) { return N-3; }
