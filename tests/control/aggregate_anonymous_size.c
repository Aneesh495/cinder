typedef struct { char code; double value; } Record;
int main(void) { return sizeof(Record) != 16 || _Alignof(Record) != 8; }
