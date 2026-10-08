int pick(int x) { switch(x) { static int n=4; case 1: return ++n; default: return n; } } int main(void) { return pick(1)!=5 || pick(1)!=6 || pick(8)!=6; }
