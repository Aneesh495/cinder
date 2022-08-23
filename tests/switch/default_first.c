int pick(int x) { int n=0; switch(x) { default: n=3; break; case 2: n=7; break; } return n; } int main(void) { return pick(2)!=7 || pick(1)!=3; }
