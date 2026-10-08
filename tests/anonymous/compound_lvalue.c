struct S{struct{int n;};};int main(void){struct S *p=&(struct S){.n=37};p->n+=2;return p->n!=39;}
