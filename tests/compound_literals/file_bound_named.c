int x[]={[5]=2}; int y[sizeof x / sizeof(int)]={[5]=4}; int main(void) { return sizeof y != 6 * sizeof(int) || y[5] != 4; }
