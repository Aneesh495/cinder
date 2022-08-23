int main(void) { int *p=0; int n=0; switch(*(p=(int[]){3,7})) { case 3: n=p[1]; break; default: n=9; } return n!=7; }
