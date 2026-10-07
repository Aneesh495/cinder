int main(void) { int a[]={1,2,3}; _Static_assert(sizeof a==12,"completed named array"); return a[0]-1; }
