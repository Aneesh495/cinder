int value(int x) { return x+1; } int (* const p)(int)=value; int main(void) { return p(102); }
