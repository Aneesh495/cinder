struct P { int x; int y; }; int main(void) { struct P p={1,2},q={3,4}; _Generic(1,int:p,default:q).x=7; return p.x!=7 || q.x!=3; }
