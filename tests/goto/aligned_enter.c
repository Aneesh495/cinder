int main(void) { int sum=0; goto inside; { _Alignas(16) int x=3; inside: x=7; sum=x; if((unsigned long)&x%16) return 1; } return sum!=7; }
