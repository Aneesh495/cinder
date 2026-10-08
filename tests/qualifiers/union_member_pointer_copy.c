union U{const int fixed;int live;};int main(void){union U u={.live=3};int *p=&u.live;int *q=p;*q=9;return u.live!=9;}
