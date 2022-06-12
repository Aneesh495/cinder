int main(void) { int a=11,b=17; unsigned long bits=(unsigned long)&a; for(int i=0;i<3;++i) { if(i==1) bits=(unsigned long)&b; } return *(int *)bits; }
