int main(void) { register int a[3]={3,5,7}; return sizeof(_Generic(1,int:a))!=12; }
