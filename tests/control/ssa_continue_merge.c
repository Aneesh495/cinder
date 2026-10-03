
int main(void) { int x=0; for(int i=0;i<9;i++) { if(i%2) continue; x+=i; } return x!=20; }
