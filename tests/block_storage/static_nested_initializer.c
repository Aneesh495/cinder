int main(void) { static int x=2; { static int x=3; static int *p=&x; ++*p; if(x!=4) return 1; } return x!=2; }
