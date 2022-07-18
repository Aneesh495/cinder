int main(void) { int (*p)[3] = &(int[3]){2,4,6}; return (*p)[1] != 4; }
