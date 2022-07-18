int main(void) { int *p=&(int){3}, *q=&(int){3}; *p=7; return p==q || *q!=3; }
