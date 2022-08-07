int main(void){_Alignas(double) char x=3; return (unsigned long)&x%8 || sizeof x!=1 || _Alignof(char)!=1;}
