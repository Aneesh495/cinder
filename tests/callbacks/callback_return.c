typedef int (*Fn)(int); int inc(int x) { return x + 1; } int dec(int x) { return x - 1; } Fn choose(int flag) { return flag ? inc : dec; } int main(void) { return choose(0)(30); }
