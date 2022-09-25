union U{const int fixed;int live;};int main(void){union U u[2]={{.live=3},{.live=5}};u[1].live=9;return u[0].live!=3||u[1].live!=9;}
