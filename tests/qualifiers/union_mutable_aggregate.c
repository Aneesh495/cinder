struct P{int x;int y;};union U{const struct P fixed;struct P live;};int main(void){union U u={.live={3,5}};struct P p={7,11};u.live=p;return u.live.x!=7||u.live.y!=11;}
