int main(void) { int *p=0; { int x=3; p=&x; goto done; } done: return *p; }
