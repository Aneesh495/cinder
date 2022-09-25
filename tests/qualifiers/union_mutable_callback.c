union U{const int fixed;int live;};void write(int *p){*p=11;}void call(void(*f)(int*),int*p){f(p);}int main(void){union U u={.live=3};call(write,&u.live);return u.live!=11;}
