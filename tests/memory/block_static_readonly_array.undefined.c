int *address(void) { static const int x[2]={3,4}; return (int *)x; } int main(void) { address()[1]=7; return 0; }
