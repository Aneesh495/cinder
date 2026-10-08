union U{const unsigned fixed;unsigned live;};int main(void){union U u={.live=0};unsigned char *p=(unsigned char*)&u.live;*p=9;return u.live!=9;}
