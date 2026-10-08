union U{const int fixed[2];int live[2];};int main(void){union U u={.fixed={3,5}};int*p=(int*)u.fixed;*p=7;return u.fixed[0];}
