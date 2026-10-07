int main(void) { int *p=0; for(int i=0; i<1 ? (p=&(int){7},1) : 0; ++i) { *p+=1; } return *p; }
