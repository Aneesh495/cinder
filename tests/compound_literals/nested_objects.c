struct S { int *p; int x[2]; }; int main(void) { struct S *p=&(struct S){.p=(int[]){5,9},.x={3,7}}; p->p[1]+=2; return p->p[0]+p->p[1]+p->x[1] != 23; }
