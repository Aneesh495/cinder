int main(void){_Alignas(16) int x=7; return (unsigned long)&x%16 || x!=7 || _Alignof(int)!=4 || sizeof x!=4;}
