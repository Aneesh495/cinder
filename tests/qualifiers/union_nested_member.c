struct S{int head;union U{const int fixed;int live;} u;};int main(void){struct S s={17,{.live=3}};s.u.live=9;return s.head!=17||s.u.live!=9;}
