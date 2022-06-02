int add(int x) { return x + 11; } int apply(int (*fn)(int),int x) { return fn(x); } int main(void) { int (*outer)(int (*)(int),int) = apply; return outer(add,32); }
