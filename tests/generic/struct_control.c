struct P { int x; int y; }; int main(void) { struct P p={1,2}; return _Generic(p,struct P:1,default:2)-1; }
