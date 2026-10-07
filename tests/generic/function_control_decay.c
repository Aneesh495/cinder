int f(int x) { return x; } int main(void) { return _Generic(f,int(*)(int):1,default:2)-1; }
