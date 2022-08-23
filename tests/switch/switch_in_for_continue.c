int main(void) { int n=0; for(int i=0;i<5;i++) { switch(i) { case 1: continue; case 3: break; default: n+=i; } n+=10; } return n!=46; }
