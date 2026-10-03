
int main(void) { unsigned char x = 255; unsigned char y = 2; return x + y != 257 || sizeof(x + y) != 4; }
