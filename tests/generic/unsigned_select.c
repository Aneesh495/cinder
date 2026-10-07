int main(void) { unsigned x=3; return _Generic(x,int:1,unsigned int:2,default:3)-2; }
