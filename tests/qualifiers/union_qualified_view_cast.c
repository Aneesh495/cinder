union U{const int fixed;int live;};int main(void){union U u={.live=3};const union U *p=&u;int *q=(int*)&p->live;*q=7;return u.live!=7;}
