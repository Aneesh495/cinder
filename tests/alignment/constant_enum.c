enum A{N=16}; int main(void){_Alignas(N) int x=7;return (unsigned long)&x%16 || x!=7;}
