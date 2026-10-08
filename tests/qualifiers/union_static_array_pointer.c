union U{const int fixed;int live;};static union U u[2]={{.live=3},{.live=5}};static int *p=&u[1].live;int main(void){*p=9;return u[0].live!=3||u[1].live!=9;}
