int pick(int x) { int n=7; switch(x) { case 1: n=9; break; } return n; } int main(void) { return pick(0)!=7 || pick(1)!=9; }
