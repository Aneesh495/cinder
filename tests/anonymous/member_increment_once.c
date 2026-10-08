struct S{struct{int n;};};int main(void){struct S a[2]={{.n=7},{.n=11}};int i=0;int old=a[i++].n++;return i!=1||old!=7||a[0].n!=8;}
