int calls; int test(void) { calls++; return 0; } int main(void) { int n=0; switch(1) { if(test()) { case 1: n=7; } else { n=9; } } return n!=7 || calls; }
