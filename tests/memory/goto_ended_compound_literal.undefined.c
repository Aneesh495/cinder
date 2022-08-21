int main(void) { int *p=0; { p=(int[2]){3,4}; goto done; } done: return p[1]; }
