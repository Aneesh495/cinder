
int main(void) { int x=0; int i=0; while(i<20) { i++; if(i==4) break; if(i%2) continue; x+=i; } return x!=2 || i!=4; }
