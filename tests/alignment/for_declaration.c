int sum=0; int main(void){for(_Alignas(16) int i=0;i<4;++i){sum+=i;if((unsigned long)&i%16)return 1;}return sum!=6;}
