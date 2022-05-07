int x=0; void update(int n) { if(n) { x+=n; update(n-1); } }
int main(void) { update(4); return x!=10; }
