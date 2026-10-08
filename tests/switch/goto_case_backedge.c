int main(void) { int n=0; int total=0; switch(1) { case 1: again: total+=n++; if(n<4) goto again; break; default: total=9; } return total!=6; }
