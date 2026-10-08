int main(void) { int n=0; switch(2) { case 2: { int x=7; n=x; goto done; } default: n=9; } done: return n!=7; }
