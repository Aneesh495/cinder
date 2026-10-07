int main(void) { int a[sizeof (int[]){[7]=2,3} / sizeof(int)] = {[8]=11}; return sizeof a != 9 * sizeof(int) || a[8] != 11; }
