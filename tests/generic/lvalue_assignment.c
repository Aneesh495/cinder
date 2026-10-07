int main(void) { int x=1,y=2; _Generic(1,int:x,default:y)=7; return x!=7 || y!=2; }
