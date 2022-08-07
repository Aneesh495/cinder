const char before=3; _Alignas(16) const int x=7; int main(void){return (unsigned long)&x%16 || x!=7 || before!=3;}
