struct S{union{const unsigned fixed:3;unsigned live:3;};char tail;};int main(void){struct S s={.live=5,.tail='Q'};s.live=7;return s.live!=7||s.tail!='Q';}
