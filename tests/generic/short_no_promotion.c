int main(void) { short x=3; return _Generic(x,short:1,int:2,default:3)-1; }
