int main(void) { int x=3; int *const p=&x; return _Generic(p,int*:1,default:2)-1; }
