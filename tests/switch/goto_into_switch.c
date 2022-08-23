int main(void) { int n=0; goto L; switch(99) { int x=99; case 1: L: x=5; n=x; break; default: n=7; } return n!=5; }
