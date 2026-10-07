int main(void) { int x=9; int **p=&(int *){&x}; **p+=5; return x!=14; }
