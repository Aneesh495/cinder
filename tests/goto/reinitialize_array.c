int main(void) { int n=0; int sum=0; again: ; int a[2]={3,4}; sum+=a[1]; a[1]=9; if(n++==0) goto again; return sum!=8; }
