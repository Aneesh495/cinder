int main(void) { int *p=0; switch(*(p=(int[]){1})) { case 1: break; } return *p; }
