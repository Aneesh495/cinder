struct P { int x; double y; }; int main(void) { int n=0; int sum=0; again: ; struct P p={3,4.0}; sum+=p.x+(int)p.y; p.x=9; if(n++==0) goto again; return sum!=14; }
