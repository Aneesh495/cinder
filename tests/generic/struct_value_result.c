struct P { int x; int y; }; struct P make(void) { return (struct P){3,4}; } int main(void) { struct P p=_Generic(1,int:make(),default:(struct P){1,2}); return p.x!=3 || p.y!=4; }
