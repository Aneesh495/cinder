union U{const unsigned fixed:3;unsigned live:3;};int main(void){union U u={.live=5};u.live=7;return u.live!=7;}
