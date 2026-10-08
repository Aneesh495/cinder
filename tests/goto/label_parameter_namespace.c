int f(int finish) { goto finish; finish=9; finish: return finish; } int main(void) { return f(3)!=3; }
