int main(void) { int *p=0; if((p=&(int){7},1)) { *p+=1; } return *p; }
