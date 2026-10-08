int first(void) { static int n=1; return n++; } int second(void) { static int n=4; return n++; } int main(void) { return first()!=1 || second()!=4 || first()!=2 || second()!=5; }
