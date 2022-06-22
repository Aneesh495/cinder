struct Data { double x; int y; }; struct Data value; int *p=&value.y; int main(void) { value.y=73; return *p; }
