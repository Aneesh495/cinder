int main(void) { float x=1.5f; return _Generic(x,float:1,double:2,default:3)-1; }
