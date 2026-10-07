int main(void) { int *p=(int *)&(const int){7}; *p=9; return *p; }
