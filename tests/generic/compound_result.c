int main(void) { int *p=&_Generic(1,int:(int){7},default:(int){9}); *p+=2; return *p-9; }
