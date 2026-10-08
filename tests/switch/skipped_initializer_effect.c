int calls; int init(void) { calls++; return 3; } int main(void) { int n=0; switch(1) { int x=init(); case 1: x=7; n=x; } return calls || n!=7; }
