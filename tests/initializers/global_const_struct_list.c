struct Data { int x; double y; }; const struct Data value={41,1.25}; int main(void) { return value.x+(int)(value.y*4.0); }
