struct S{struct{long n;double weight;};};static int read(struct S s){return s.n==41&&s.weight==2.5;}int main(void){struct S s={.n=41,.weight=2.5};return !read(s);}
