int main(void) { char x=3; return _Generic(x,char:1,int:2,default:3)-1; }
