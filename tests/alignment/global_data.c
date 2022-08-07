char before=3; _Alignas(16) int x=7; char after=5; int main(void){return (unsigned long)&x%16 || x!=7 || before!=3 || after!=5;}
