int main(void) { static const int x=3; static const int *p=&x; return *p!=3; }
