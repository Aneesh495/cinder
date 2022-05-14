struct Record { char first, second; long count; int flags, category; };
int main(void) { return sizeof(struct Record) != 24 || _Alignof(struct Record) != 8; }
