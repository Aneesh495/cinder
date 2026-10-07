int x=29; struct Data { int *p; int y; }; struct Data value={&x,31}; int main(void) { return *value.p+value.y; }
