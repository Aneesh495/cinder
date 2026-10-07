int main(void) { int a[_Generic(1,int:3,default:5)]={1,2,3}; return sizeof a!=12 || a[2]!=3; }
