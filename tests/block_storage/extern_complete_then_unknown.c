int x[3]={2,5,7}; int main(void) { extern int x[3]; extern int x[]; return sizeof(x)!=12 || x[2]!=7; }
