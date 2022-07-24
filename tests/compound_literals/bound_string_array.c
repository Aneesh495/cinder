int main(void) { int a[sizeof (char[]){"abc"}] = {[3]=8}; return sizeof a != 4 * sizeof(int) || a[3] != 8; }
