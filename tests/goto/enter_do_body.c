int main(void) { int n=0; goto inside; do { inside: ++n; } while(n<3); return n!=3; }
