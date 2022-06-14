struct Data; struct Data *p; struct Data { int x; }; int main(void) { struct Data value; value.x=91; p=&value; return p->x; }
