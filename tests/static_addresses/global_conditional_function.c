int left(int x) { return x+1; } int right(int x) { return x+3; } int (*p)(int)=0?left:right; int main(void) { return p(128); }
