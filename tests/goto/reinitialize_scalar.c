int main(void) { int n=0; int sum=0; again: ; int x=3; sum+=x; x=7; if(n++==0) goto again; return sum!=6; }
