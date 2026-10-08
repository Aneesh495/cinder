int main(void) { int n=0; int x=3; int sum=0; again: ; int *p=&x; sum+=*p; p=0; if(n++==0) goto again; return sum!=6; }
