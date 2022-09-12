struct S{struct{int n;};};int main(void){register struct S s={.n=7};return *&s.n;}
