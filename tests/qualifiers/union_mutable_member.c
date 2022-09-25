union U{const int fixed;int live;};int main(void){union U u={.live=3};u.live=7;return u.live!=7;}
