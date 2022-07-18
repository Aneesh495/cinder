int main(void) { int *p=0; for(int i=0;i<2;++i) { if(i!=0) return *p; p=&(int){9}; } return 0; }
