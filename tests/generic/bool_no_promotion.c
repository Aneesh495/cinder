int main(void) { _Bool x=1; return _Generic(x,_Bool:1,int:2,default:3)-1; }
