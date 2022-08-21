int main(void) { int sum=0; goto inside; { int x=3; inside: x=7; sum=x; } return sum!=7; }
