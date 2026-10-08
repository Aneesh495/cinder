int pick(_Bool x) { switch(x) { case 0: return 4; case 1: return 7; case 2: return 9; } return 3; } int main(void) { return pick(0)!=4 || pick(8)!=7; }
