struct S{union{const int fixed;int live;};int tail;};int main(void){struct S s={.live=3,.tail=17};s.live=9;return s.live!=9||s.tail!=17;}
