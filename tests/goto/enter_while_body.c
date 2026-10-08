int main(void) { int n=0; goto inside; while(n<2) { inside: ++n; } return n!=2; }
