struct Data { const char *p; int x; }; struct Data value={"abc",37}; int main(void) { return value.p[2]-90+value.x; }
