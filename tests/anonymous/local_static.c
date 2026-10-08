struct S{struct{int n;};};static int f(void){static struct S s={.n=61};return ++s.n;}int main(void){return f()!=62||f()!=63;}
