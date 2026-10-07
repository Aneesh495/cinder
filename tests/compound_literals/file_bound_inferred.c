int a[sizeof (int[]){1,2,3} / sizeof(int)] = {4,5,6}; int main(void) { return sizeof a != 3 * sizeof(int) || a[2] != 6; }
