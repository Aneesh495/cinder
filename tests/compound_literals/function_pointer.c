typedef int (*Callback)(int); int add(int x) { return x+7; } int main(void) { Callback *p=&(Callback){add}; return (*p)(3)!=10; }
