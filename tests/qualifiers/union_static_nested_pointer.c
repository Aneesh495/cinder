struct S{int head;union U{const int fixed;int live;} u;};static struct S s={17,{.live=3}};static int *p=&s.u.live;int main(void){*p=9;return s.head!=17||s.u.live!=9;}
