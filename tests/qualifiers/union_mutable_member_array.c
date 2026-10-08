union U{const int fixed[2];int live[2];};int main(void){union U u={.live={3,5}};u.live[1]=9;return u.live[0]!=3||u.live[1]!=9;}
