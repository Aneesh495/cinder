int main(void){char a=3; _Alignas(16) char b=4; long c=5; _Alignas(16) int d=6; return a!=3 || b!=4 || c!=5 || d!=6 || (unsigned long)&b%16 || (unsigned long)&d%16;}
