int main(void) { int x[3]={1,2,3},y[2]={4,5}; return _Generic(1,int:x,default:y)[2]-3; }
