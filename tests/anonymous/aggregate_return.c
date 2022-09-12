struct S{struct{long n;double weight;};};static struct S make(void){struct S s={.n=43,.weight=3.25};return s;}int main(void){struct S s=make();return s.n!=43||s.weight!=3.25;}
