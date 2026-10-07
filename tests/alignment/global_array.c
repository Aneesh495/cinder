_Alignas(16) int a[3]={1,2,3}; int main(void){return (unsigned long)a%16 || sizeof a!=12 || a[2]!=3;}
