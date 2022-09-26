unsigned work(unsigned x,int p){unsigned a=x+7;if(p)return a+(x+7);return a;}int main(void){return work(3,1)+work(3,0);}
