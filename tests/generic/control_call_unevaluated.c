int calls; int f(void) { ++calls; return 1; } int main(void) { int n=_Generic(f(),int:7,default:9); return calls || n!=7; }
