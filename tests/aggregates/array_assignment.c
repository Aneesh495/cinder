struct P { int a[4]; };
int main(void) { struct P p={{1,2,3,4}}, q={{4,5,6,7}}; p=q; return p.a[0]+p.a[1]+p.a[2]+p.a[3]; }
