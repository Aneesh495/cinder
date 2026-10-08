int main(void) { static int x=3; static int *p=&_Generic(0,int:x,default:x); return *p!=3; }
