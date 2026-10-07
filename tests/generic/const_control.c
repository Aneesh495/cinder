int main(void) { const int x=3; return _Generic(x,int:1,const int:2,default:3)-1; }
