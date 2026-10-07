int main(void) { const int x=1; _Generic(1,int:x,default:x)=3; return 0; }
