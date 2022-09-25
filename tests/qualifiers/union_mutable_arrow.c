union U{const int fixed;int live;};int main(void){union U u={.live=3};union U *p=&u;p->live+=5;return u.live!=8;}
