struct S{int head;union U{const int fixed;int live;} u;};static struct S s={17,{.fixed=3}};static int*p=(int*)&s.u.fixed;int main(void){*p=7;return s.u.fixed;}
