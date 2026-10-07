int main(void) { return _Generic(1,int:0,default:(int[1]){1,2}[0]); }
