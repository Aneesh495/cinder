int main(void) { int n=0; switch(2) { case 2: for(int i=0;i<8;i++) { n++; if(i==2) break; } n+=4; break; default: n=99; } return n!=7; }
