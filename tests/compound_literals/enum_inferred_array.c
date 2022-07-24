int main(void) { enum { N=sizeof (int[]){[4]=3} / sizeof(int) }; int a[N]={[4]=9}; return N != 5 || a[4] != 9; }
