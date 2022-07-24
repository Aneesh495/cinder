int main(void) { int x[]={1,2,3}; int y[sizeof x / sizeof(int)]={4,5,6}; return sizeof y != 3 * sizeof(int) || y[2] != 6; }
