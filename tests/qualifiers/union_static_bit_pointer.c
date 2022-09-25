union U{const unsigned fixed:3;unsigned live:3;};static union U u={.live=5};static union U*p=&u;int main(void){p->live=7;return u.live!=7;}
