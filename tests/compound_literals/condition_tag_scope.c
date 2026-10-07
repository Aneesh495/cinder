struct S { long x; }; int main(void) { int size=0; if(sizeof(struct S { char x; })) size=sizeof(struct S); struct S after; return size!=1 || sizeof(after)!=8; }
