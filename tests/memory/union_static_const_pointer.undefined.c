union U{const int fixed;int live;};static union U u={.fixed=3};static int*p=(int*)&u.fixed;int main(void){*p=7;return u.fixed;}
