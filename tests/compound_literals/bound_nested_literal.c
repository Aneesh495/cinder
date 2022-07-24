int main(void) { int a[sizeof (int[]){(int){7},(int){8}} / sizeof(int)] = {1,2}; return sizeof a != 2 * sizeof(int) || a[1] != 2; }
