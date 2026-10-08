int pick(int x) { int n=0; switch(x) { case 1: n=2; break; default: n=3; case 4: n+=5; } return n; } int main(void) { return pick(1)!=2 || pick(9)!=8 || pick(4)!=5; }
