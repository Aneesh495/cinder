static volatile int flag=1;union U{const int locked;int open;};int main(void){union U u={.open=37};int *p=&u.open,*q;if(flag)q=p;else q=p;*q=41;return u.open;}
