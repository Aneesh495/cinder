int pick(signed char x) { switch(x) { case -1: return 4; case 255: return 7; default: return 9; } } int main(void) { return pick(-1)!=4 || pick(2)!=9; }
