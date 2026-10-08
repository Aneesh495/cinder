int main(void) { int n=0; int *saved=0; again: ; int *p=(int[2]){3,4}; if(n++==0) { saved=p; p[1]=9; goto again; } return saved!=p || p[1]!=4; }
