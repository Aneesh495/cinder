typedef unsigned char Bytes[sizeof(long) + _Alignof(double)];
int main(void) { return sizeof(Bytes) != 16; }
