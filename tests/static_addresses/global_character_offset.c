int value=0x1234; unsigned char *p=(unsigned char *)&value+1; int main(void) { return p[0]; }
