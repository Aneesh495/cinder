union U{const int fixed;int live;};int main(void){union U u={.fixed=3};int*p=(int*)&u.fixed;*p=7;return u.fixed;}
