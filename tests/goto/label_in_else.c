int main(void) { int n=0; goto inside; if(1) { n=9; } else { inside: n=3; } return n!=3; }
