int main(void) { int sum=0; for(int i=0;i<4;++i) { if(i<2) goto next; sum+=i; next: continue; } return sum!=5; }
