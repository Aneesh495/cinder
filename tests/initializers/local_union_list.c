union Data { int x; double y; }; int main(void) { union Data value={53}; return value.x; }
