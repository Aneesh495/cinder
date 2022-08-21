int main(void) { int sum=0; for(int i=0;i<4;++i) { for(int j=0;j<4;++j) { sum+=i+j; if(i==1 && j==2) goto finish; } } finish: return sum!=12; }
