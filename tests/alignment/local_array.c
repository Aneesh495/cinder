int main(void){_Alignas(16) int x[3]={1,2,3}; return (unsigned long)x%16 || sizeof x!=12 || _Alignof(int[3])!=4 || x[2]!=3;}
