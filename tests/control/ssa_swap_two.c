
int main(void) { int a=2; int b=7; for(int i=0;i<3;i++) { int t=a; a=b; b=t; } return a!=7 || b!=2; }
