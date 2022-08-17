int f(void) { extern int x[]; extern int x[3]; return sizeof(x)!=12 || x[2]!=7; } int x[3]={2,5,7}; int main(void) { return f(); }
