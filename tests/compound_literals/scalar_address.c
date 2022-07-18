int main(void) { int *p = &(int){7}; *p += 5; return *p != 12; }
