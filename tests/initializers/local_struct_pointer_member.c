struct Data { int *p; int x; }; int main(void) { int x=19; struct Data value={&x,23}; return *value.p+value.x; }
