union U{const unsigned fixed;unsigned live;};int main(void){union U u={.fixed=3};unsigned char*p=(unsigned char*)&u.fixed;*p=7;return u.fixed;}
