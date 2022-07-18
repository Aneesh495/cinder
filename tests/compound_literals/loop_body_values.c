int main(void) { int sum=0; for(int i=0;i<4;++i) { int *p=&(int){i+3}; sum+=*p; *p+=1; sum+=*p; } return sum!=40; }
