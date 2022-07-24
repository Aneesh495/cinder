int main(void) { int a[sizeof *(int[]){1,2}]={4}; return sizeof a != sizeof(int) * sizeof(int); }
