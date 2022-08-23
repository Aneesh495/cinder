int pick(int x) { switch(x) { default: return 7; case 1: return 3; case 2: return 5; } } int main(void) { return pick(1)!=3 || pick(2)!=5 || pick(9)!=7; }
