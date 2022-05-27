union U { unsigned int word; unsigned char bytes[4]; }; int main(void) { union U u; u.word = 0x12345678U; return u.bytes[2]; }
