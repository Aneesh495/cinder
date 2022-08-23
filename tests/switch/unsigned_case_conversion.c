int pick(unsigned int x) { switch(x) { case -1: return 4; case 1: return 7; default: return 9; } } int main(void) { return pick(4294967295U)!=4 || pick(1U)!=7 || pick(8U)!=9; }
