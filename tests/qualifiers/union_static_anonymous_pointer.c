struct S{union{const int fixed;int live;};char tail;};static struct S s={.live=3,.tail='Q'};static int *p=&s.live;int main(void){*p=9;return s.live!=9||s.tail!='Q';}
