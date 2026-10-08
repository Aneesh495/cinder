int add(int n) { static int total; total+=n; if(n>0) return add(n-1); return total; } int main(void) { return add(3)!=6 || add(2)!=9; }
