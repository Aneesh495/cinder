int target=17; union U { int *p; long x; }; union U u={.x=123,.p=&target}; int main(void) { return *u.p; }
