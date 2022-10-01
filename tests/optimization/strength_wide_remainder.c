static volatile unsigned long long input=18446744073709551615ULL; int main(void){unsigned long long x=input;return x%9223372036854775808ULL==9223372036854775807ULL?0:1;}
