
int main(void) { unsigned char x = 255; int old = x++; return old != 255 || x != 0; }
