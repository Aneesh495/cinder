struct S{union{int n;double weight;};};int main(void){struct S s={.weight=3.25};return s.weight!=3.25;}
