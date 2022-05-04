
int main(void) { return _Alignof(char) != 1 || _Alignof(short) != 2 || _Alignof(long) != 8 || _Alignof(double) != 8; }
