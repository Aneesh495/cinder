union U{const unsigned fixed:3;unsigned live:3;};int main(void){union U u[2]={{.live=3},{.live=5}};u[1].live++;return u[0].live!=3||u[1].live!=6;}
