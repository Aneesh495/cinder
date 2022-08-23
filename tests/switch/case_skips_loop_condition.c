int calls; int test(void) { calls++; return 0; } int main(void) { int n=0; switch(1) { while(test()) { case 1: n=7; break; } } return n!=7 || calls; }
