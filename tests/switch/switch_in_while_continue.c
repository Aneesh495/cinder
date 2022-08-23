int main(void) { int i=0; int n=0; while(i<4) { i++; switch(i) { case 2: continue; default: n+=i; break; } n++; } return n!=11; }
