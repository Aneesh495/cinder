struct P { int x; int y; }; int main(void) { struct P p={1,2}; int a[sizeof (struct P[]){_Generic(1,int:p,default:p),p}/sizeof(struct P)]={3,4}; return sizeof a!=8; }
