
int main(void) { double a=1.25; double b=2.5; double c=3.75; for(int i=0;i<7;i++) { double t=a; a=b; b=c; c=t; } return a!=2.5 || b!=3.75 || c!=1.25; }
