struct P{int x;};union U{const struct P fixed;struct P live;};int main(void){union U u={.fixed={3}};struct P*p=(struct P*)&u.fixed;p->x=7;return u.fixed.x;}
