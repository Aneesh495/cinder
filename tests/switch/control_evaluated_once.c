int calls; int control(void) { calls++; return 2; } int main(void) { int n=0; switch(control()) { case 1: n=8; break; case 2: n=4; break; case 3: n=9; } return calls!=1 || n!=4; }
