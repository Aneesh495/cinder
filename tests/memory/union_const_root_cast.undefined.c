union U{const int fixed;int live;};int main(void){const union U u={.live=3};union U*p=(union U*)&u;p->live=7;return u.live;}
