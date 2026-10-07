int main(void) { int *p=0; return _Generic(*p,int:1,default:2)-1; }
