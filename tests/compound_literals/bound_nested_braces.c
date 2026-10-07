int main(void) { int a[sizeof (int[][2]){{1,2},{3,4},{5}} / sizeof(int)] = {[5]=9}; return sizeof a != 6 * sizeof(int) || a[5] != 9; }
