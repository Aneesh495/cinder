int take(int a,double b,int c) { return a + (int)b + c + 1; }
int main(void) { return take(1, 2.5f, (unsigned char)3) != 7; }
