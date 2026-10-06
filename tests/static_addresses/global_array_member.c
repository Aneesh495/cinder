struct Data { int x; int y; }; struct Data data[3]; int *p=&data[2].y; int main(void) { data[2].y=83; return *p; }
