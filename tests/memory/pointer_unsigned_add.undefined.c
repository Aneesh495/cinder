int main(void) { int data[2]; data[0]=7; data[1]=9; unsigned long index=18446744073709551615UL; return *(&data[1]+index); }
