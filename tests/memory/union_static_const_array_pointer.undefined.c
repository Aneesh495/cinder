union U{const int fixed[2];int live[2];};static union U u={.fixed={3,5}};static int*p=(int*)&u.fixed[1];int main(void){*p=7;return u.fixed[1];}
