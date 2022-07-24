int main(void) { char x[]="hello"; int y[sizeof x]={[5]=7}; return sizeof y != 6 * sizeof(int) || y[5] != 7; }
