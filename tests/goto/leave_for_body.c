int main(void) { int sum=0; for(int i=0;i<3;++i) { sum+=i; if(i==1) goto done; } done: return sum!=1; }
