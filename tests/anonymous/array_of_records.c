struct S{struct{int x,y;};};int main(void){struct S a[2]={{.x=7,.y=11},{.y=17}};return a[0].x+a[1].y!=24||a[1].x!=0;}
