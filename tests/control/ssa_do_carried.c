
int main(void) { int a=1; int b=2; int i=0; do { int t=a; a=b; b=t; i++; } while(i<3); return a!=2 || b!=1; }
