union U{const int fixed;int live;};static union U u={.live=3};static int *p=&u.live;int main(void){*p=9;return u.live!=9;}
