struct S{union{int n;long other;};};int main(void){int *p;{struct S s={.n=7};p=&s.n;}return *p;}
