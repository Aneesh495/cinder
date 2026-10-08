struct S{struct{int n;};};int main(void){struct S s={.n=17};struct S *p=&s;p->n+=3;return s.n!=20;}
