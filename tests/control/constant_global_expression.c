enum { Base = 7 };
long first = Base * 3 + (int)sizeof(long);
unsigned int second = 4294967295U / 5U;
int main(void) { return first != 29 || second != 858993459U; }
