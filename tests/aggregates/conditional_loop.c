struct P { int a[2]; };
int main(void) { struct P a={{2,3}}, b={{4,5}}; int sum=0; for(int i=0;i<4;++i) sum+=(i&1?a:b).a[0]; return sum; }
