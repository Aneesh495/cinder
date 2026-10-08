int main(void) { int n=0; goto inside; if(0) { n=9; inside: n=3; } return n!=3; }
