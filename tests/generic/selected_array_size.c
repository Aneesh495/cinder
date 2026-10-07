int main(void) { int x[3]={1,2,3},y[2]={4,5}; return sizeof _Generic(1,int:x,default:y)!=12; }
