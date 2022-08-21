int main(void) { int n=0; goto a; c: return n!=3; b: ++n; goto c; a: n=2; goto b; }
