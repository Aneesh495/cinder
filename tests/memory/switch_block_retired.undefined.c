int main(void) { int *p=0; switch(1) { case 1: { int n=7; p=&n; break; } } return *p; }
