union Data { int x; double y; }; int main(void) { union Data value={.y=1.5}; return (int)(value.y*4.0); }
