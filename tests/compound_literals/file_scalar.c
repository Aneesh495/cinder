int *p=&(int){41}; int main(void) { *p+=1; return *p!=42; }
