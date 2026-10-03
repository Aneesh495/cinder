
int main(void) { int a=1; int b=2; int c=3; for(int i=0;i<5;i++) { int t=a; a=b; b=c; c=t; } return a!=3 || b!=1 || c!=2; }
