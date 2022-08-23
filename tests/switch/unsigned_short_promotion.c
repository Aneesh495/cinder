int pick(unsigned short x) { switch(x) { case 65535: return 4; case -1: return 7; default: return 9; } } int main(void) { return pick(65535)!=4 || pick(2)!=9; }
