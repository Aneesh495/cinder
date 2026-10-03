int take(unsigned char x, unsigned short y) { return x + y; }
int main(void) { return take(300, 65537U) != 45; }
