int main(void) { int n=1; int v=0; switch(n+=2,n) { case 3: v=n; break; default: v=8; } return v!=3 || n!=3; }
