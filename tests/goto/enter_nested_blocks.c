int main(void) { int sum=0; goto inside; { int x=3; { int y=4; inside: x=5; y=7; sum=x+y; } } return sum!=12; }
