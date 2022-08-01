int main(void) { return _Generic(1,struct P { int x; }:(struct P){7},int:(int){0}); }
