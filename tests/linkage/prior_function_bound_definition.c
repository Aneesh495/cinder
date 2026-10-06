int value(int (*p)[3]); int value(int (*actual)[]) { return (*actual)[2]; } int main(void) { int data[3]; data[2]=89; return value(&data); }
