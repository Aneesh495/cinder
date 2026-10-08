int pick(int x) { switch(x) { case -7: return 3; case 0: return 5; case 12: return 9; default: return 11; } } int main(void) { return pick(-7)!=3 || pick(0)!=5 || pick(12)!=9 || pick(4)!=11; }
