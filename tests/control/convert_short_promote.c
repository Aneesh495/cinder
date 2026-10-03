
int main(void) { unsigned short x = 65535; return -x != -65535 || sizeof(~x) != 4; }
