int pick(int x) { int n=0; switch(x) { case 1: n+=2; case 2: n+=3; case 3: n+=5; break; default: n=11; } return n; } int main(void) { return pick(1)!=10 || pick(2)!=8 || pick(3)!=5 || pick(7)!=11; }
