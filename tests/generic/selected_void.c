int x; void f(void) { ++x; } int main(void) { _Generic(1,int:f(),default:(void)0); return x-1; }
