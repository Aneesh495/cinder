int main(void) { int a[sizeof (double){1.0+2.0}]={3}; return sizeof a != 8 * sizeof(int) || a[0] != 3; }
