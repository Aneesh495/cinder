union U{const int fixed[2];int live[2];};static union U u={.live={3,5}};static int *p=&u.live[1];int main(void){*p=9;return u.live[0]!=3||u.live[1]!=9;}
