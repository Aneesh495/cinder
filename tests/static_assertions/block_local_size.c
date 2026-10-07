int main(void) { int a[3]={1,2,3}; _Static_assert(sizeof a==12,"local array"); return a[2]-3; }
