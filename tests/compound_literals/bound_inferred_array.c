int main(void) { int a[sizeof (int[]){1,2,3} / sizeof(int)] = {4,5,6}; return a[2] - 6; }
