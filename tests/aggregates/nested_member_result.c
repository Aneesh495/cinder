struct P { int x; int a[2]; }; struct Q { struct P p; int z; };
int main(void) { struct Q a={{4,{2,3}},1}, b={{7,{8,9}},2}; struct P p=(0?a:b).p; return p.x+p.a[1]; }
