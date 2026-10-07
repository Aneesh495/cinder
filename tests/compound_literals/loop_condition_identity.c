int main(void) { int *p=0,*prior=0; int sum=0; for(int i=0; i<3 ? (p=&(int){i+7},1) : 0; ++i) { if(prior!=0 && p!=prior) return 1; prior=p; sum+=*p; } return sum!=24; }
