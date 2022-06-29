int target; union U { int *p; int x; }; union U u={.p=&target,.x=17}; int main(void) { return u.x; }
